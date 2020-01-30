/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include "mongo/client/replica_set_monitor.h"

#include <algorithm>
#include <limits>
#include <random>

#include "mongo/bson/simple_bsonelement_comparator.h"
#include "mongo/client/connpool.h"
#include "mongo/client/global_conn_pool.h"
#include "mongo/client/read_preference.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/bson_extract_optime.h"
#include "mongo/db/server_options.h"
#include "mongo/executor/network_interface_factory.h"
#include "mongo/executor/network_interface_thread_pool.h"
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/platform/mutex.h"
#include "mongo/rpc/metadata/egress_metadata_hook_list.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/util/background.h"
#include "mongo/util/concurrency/thread_pool.h"
#include "mongo/util/debug_util.h"
#include "mongo/util/exit.h"
#include "mongo/util/fail_point.h"
#include "mongo/util/log.h"
#include "mongo/util/string_map.h"
#include "mongo/util/timer.h"

namespace mongo {
using namespace mongo::sdam;

using std::numeric_limits;
using std::set;
using std::shared_ptr;
using std::string;
using std::vector;

// Failpoint for changing the default refresh period
MONGO_FAIL_POINT_DEFINE(modifyReplicaSetMonitorDefaultRefreshPeriod);

namespace {
// Pull nested types to top-level scope
using executor::TaskExecutor;
using CallbackArgs = TaskExecutor::CallbackArgs;
using CallbackHandle = TaskExecutor::CallbackHandle;

const ReadPreferenceSetting kPrimaryOnlyReadPreference(ReadPreference::PrimaryOnly, TagSet());

// Utility functions to use when finding servers
static auto minWireCompare = [](const ServerDescriptionPtr& a, const ServerDescriptionPtr& b) {
    return a->getMinWireVersion() < b->getMinWireVersion();
};

static auto maxWireCompare = [](const ServerDescriptionPtr& a, const ServerDescriptionPtr& b) {
    return a->getMaxWireVersion() < b->getMaxWireVersion();
};

static auto secondaryPredicate = [](const ServerDescriptionPtr& server) {
    return server->getType() == ServerType::kRSPrimary;
};

std::string debugReadPref(const ReadPreferenceSetting& readPref) {
    std::stringstream s;
    s << readPref.toString();
    if (!readPref.minOpTime.isNull()) {
        s << "; minOpTime - " << readPref.minOpTime;
    }
    return s.str();
}
}  // namespace

ReplicaSetMonitor::ReplicaSetMonitor(const MongoURI& uri, std::shared_ptr<TaskExecutor> executor)
    : _serverSelector(std::make_unique<SdamServerSelector>(kServerSelectionConfig)),
      _uri(uri),
      _executor(_initTaskExecutor(executor)) {

    // TODO: sdam should use the HostAndPort type for ServerAddress
    std::vector<ServerAddress> seeds;
    for (const auto seed : uri.getServers()) {
        seeds.push_back(seed.toString());
    }

    _sdamConfig = SdamConfiguration(seeds);
    _clockSource = getGlobalServiceContext()->getPreciseClockSource();
}

ReplicaSetMonitorPtr ReplicaSetMonitor::make(const MongoURI& uri,
                                             std::shared_ptr<TaskExecutor> executor) {
    auto result = std::make_shared<ReplicaSetMonitor>(uri, executor);
    result->init();
    return result;
}

void ReplicaSetMonitor::init() {
    stdx::lock_guard<Mutex> lock(_mutex);
    LOG(0) << "Starting Replica Set Monitor " << _uri;

    _eventsPublisher = std::make_shared<sdam::TopologyEventsPublisher>(_executor);
    _topologyManager =
        std::make_unique<TopologyManager>(_sdamConfig, _clockSource, _eventsPublisher);
    _isMasterMonitor = std::make_unique<ServerIsMasterMonitor>(
        _sdamConfig, _eventsPublisher, _topologyManager->getTopologyDescription());

    _eventsPublisher->registerListener(shared_from_this());
    _eventsPublisher->registerListener(_isMasterMonitor);
    _isClosed = false;

    _startOutstandingQueryProcessor();
    globalRSMonitorManager.getNotifier().onFoundSet(getName());
}

void ReplicaSetMonitor::close() {
    {
        stdx::lock_guard<Mutex> lock(_mutex);
        _logDebug() << "Closing Replica Set Monitor " << getName();
        _isClosed = true;
        _isMasterMonitor->close();
        _eventsPublisher->close();
        _failOutstandingWitStatus(
            Status{ErrorCodes::ShutdownInProgress, "the ReplicaSetMonitor is shutting down"});
    }

    globalRSMonitorManager.getNotifier().onDroppedSet(getName());

    _outstandingQueriesCV.notify_all();
    _queryProcessorThread.join();

    _logDebug() << "Done closing Replica Set Monitor " << getName();
}

SemiFuture<HostAndPort> ReplicaSetMonitor::getHostOrRefresh(const ReadPreferenceSetting& criteria,
                                                            Milliseconds maxWait) {
    using future_type = HostAndPort;
    _logDebug() << "getHostOrRefresh: " << debugReadPref(criteria) << "; maxWait - " << maxWait;

    {
        // TODO: remove this mutex acquisition
        stdx::lock_guard<Mutex> lock(_mutex);
        if (_isClosed) {
            return SemiFuture<future_type>(Status(ErrorCodes::ReplicaSetMonitorRemoved,
                                                  str::stream() << "ReplicaSetMonitor for set "
                                                                << getName() << " is removed"));
        }
    }

    // try to satisfy query immediately
    const auto& immediateResult = _getHost(criteria);
    if (immediateResult) {
        return {std::move(*immediateResult)};
    }

    // fail fast on timeout
    const auto deadline = _clockSource->now() + maxWait;
    if (deadline - _clockSource->now() < Milliseconds(0)) {
        return SemiFuture<HostAndPort>(_makeUnsatisfiedReadPrefError(criteria));
    }

    // otherwise, do async version
    Future<future_type> result;
    {
        mongo::stdx::lock_guard<Mutex> lk(_mutex);
        auto pf = makePromiseFuture<future_type>();
        auto query = std::make_shared<SingleHostQuery>();
        query->type = HostQueryType::SINGLE;
        query->deadline = deadline;
        query->criteria = criteria;
        query->promise = std::move(pf.promise);

        auto deadlineCb = [query,
                           self = shared_from_this()](const TaskExecutor::CallbackArgs& cbArgs) {
            stdx::lock_guard<Mutex> lock(self->_mutex);
            if (query->done) {
                return;
            }

            const auto cbStatus = cbArgs.status;
            if (!cbStatus.isOK()) {
                query->promise.setError(cbStatus);
                query->done = true;
                return;
            }

            const auto errorStatus = self->_makeUnsatisfiedReadPrefError(query->criteria);
            query->promise.setError(errorStatus);
            query->done = true;
            self->_logDebug() << "timeout: " << errorStatus.toString();
        };
        auto swDeadlineHandle = _executor->scheduleWorkAt(query->deadline, deadlineCb);

        if (!swDeadlineHandle.isOK()) {
            _logDebug() << "error scheduling deadline handler: " << swDeadlineHandle.getStatus();
            return SemiFuture<future_type>::makeReady(swDeadlineHandle.getStatus());
        }
        query->deadlineHandle = swDeadlineHandle.getValue();

        _outstandingQueries.emplace_back(query);
        result = std::move(pf.future);
    }

    _outstandingQueriesCV.notify_all();
    return std::move(result).semi();
}


// TODO: get rid of this; change ServerAddress underlying type to HostAndPort
std::vector<HostAndPort> ReplicaSetMonitor::_extractHosts(
    const std::vector<ServerDescriptionPtr>& serverDescriptions) {
    std::vector<HostAndPort> result;
    for (const auto server : serverDescriptions) {
        result.push_back(HostAndPort(server->getAddress()));
    }
    return result;
}

SemiFuture<std::vector<HostAndPort>> ReplicaSetMonitor::getHostsOrRefresh(
    const ReadPreferenceSetting& criteria, Milliseconds maxWait) {
    using future_type = std::vector<HostAndPort>;
    _logDebug() << "getHostsOrRefresh: " << debugReadPref(criteria) << "; maxWait - " << maxWait;

    {
        // TODO: remove this mutex acquisition
        stdx::lock_guard<Mutex> lock(_mutex);
        if (_isClosed) {
            return SemiFuture<future_type>(Status(ErrorCodes::ReplicaSetMonitorRemoved,
                                                  str::stream() << "ReplicaSetMonitor for set "
                                                                << getName() << " is removed"));
        }
    }

    // try to satisfy query immediately
    const auto& immediateResult = _getHosts(criteria);
    if (immediateResult) {
        return {std::move(*immediateResult)};
    }

    // fail fast on timeout
    const auto deadline = _clockSource->now() + maxWait;
    if (deadline - _clockSource->now() < Milliseconds(0)) {
        return SemiFuture<std::vector<HostAndPort>>(_makeUnsatisfiedReadPrefError(criteria));
    }

    // otherwise, do async version
    Future<future_type> result;
    {
        mongo::stdx::lock_guard<Mutex> lk(_mutex);
        auto pf = makePromiseFuture<future_type>();
        auto query = std::make_shared<MultiHostQuery>();
        query->type = HostQueryType::MULTI;
        query->deadline = deadline;
        query->criteria = criteria;
        query->promise = std::move(pf.promise);

        auto deadlineCb = [query,
                           self = shared_from_this()](const TaskExecutor::CallbackArgs& cbArgs) {
            mongo::stdx::lock_guard<Mutex> lock(self->_mutex);
            if (query->done) {
                return;
            }

            const auto cbStatus = cbArgs.status;
            if (!cbStatus.isOK()) {
                query->promise.setError(cbStatus);
                query->done = true;
                return;
            }

            const auto errorStatus = self->_makeUnsatisfiedReadPrefError(query->criteria);
            query->promise.setError(errorStatus);
            query->done = true;
            self->_logDebug() << "timeout: " << errorStatus.toString();
        };
        auto swDeadlineHandle = _executor->scheduleWorkAt(query->deadline, deadlineCb);

        if (!swDeadlineHandle.isOK()) {
            log() << "error scheduling deadline handler: " << swDeadlineHandle.getStatus();
            return SemiFuture<future_type>::makeReady(swDeadlineHandle.getStatus());
        }
        query->deadlineHandle = swDeadlineHandle.getValue();

        _outstandingQueries.emplace_back(query);
        result = std::move(pf.future);
    }

    _outstandingQueriesCV.notify_all();
    return std::move(result).semi();
}

boost::optional<std::vector<HostAndPort>> ReplicaSetMonitor::_getHosts(
    const ReadPreferenceSetting& criteria) {
    auto result = _serverSelector->selectServers(_currentTopology(), criteria);
    if (result) {
        std::stringstream buf;
        for (auto serverDescription : *result) {
            buf << serverDescription->getAddress() << ", ";
        }
        log() << getName() << " getHosts; " << debugReadPref(criteria) << "; result - "
              << ((result) ? buf.str() : "");
        return _extractHosts(*result);
    }
    return boost::none;
}

boost::optional<HostAndPort> ReplicaSetMonitor::_getHost(const ReadPreferenceSetting& criteria) {
    auto currentTopology = _currentTopology();
    auto result = _serverSelector->selectServer(currentTopology, criteria);
    if (result)
        log() << getName() << " getHost; " << debugReadPref(criteria) << "; result - "
              << ((result) ? (*result)->getAddress() : "");
    return result ? boost::optional<HostAndPort>((*result)->getAddress()) : boost::none;
}

HostAndPort ReplicaSetMonitor::getMasterOrUassert() {
    return getHostOrRefresh(kPrimaryOnlyReadPreference).get();
}

void ReplicaSetMonitor::failedHost(const HostAndPort& host, const Status& status) {
    IsMasterOutcome outcome(host.toString(), status.toString());
    _topologyManager->onServerDescription(outcome);
}

boost::optional<ServerDescriptionPtr> ReplicaSetMonitor::_currentPrimary() const {
    return _currentTopology()->getPrimary();
}

bool ReplicaSetMonitor::isPrimary(const HostAndPort& host) const {
    const auto currentPrimary = _currentPrimary();
    return (currentPrimary ? (*currentPrimary)->getAddress() == host.toString() : false);
}

bool ReplicaSetMonitor::isHostUp(const HostAndPort& host) const {
    const boost::optional<ServerDescriptionPtr>& serverDescription =
        _currentTopology()->findServerByAddress(host.toString());
    return serverDescription != boost::none &&
        (*serverDescription)->getType() != ServerType::kUnknown;
}

int ReplicaSetMonitor::getMinWireVersion() const {
    const std::vector<ServerDescriptionPtr>& servers = _currentTopology()->getServers();
    if (servers.size() > 0) {
        const auto serverDescription =
            *std::min_element(servers.begin(), servers.end(), minWireCompare);
        return serverDescription->getMinWireVersion();
    } else {
        return 0;
    }
}

int ReplicaSetMonitor::getMaxWireVersion() const {
    const std::vector<ServerDescriptionPtr>& servers =
        _topologyManager->getTopologyDescription()->getServers();
    if (servers.size() > 0) {
        const auto serverDescription =
            *std::max_element(servers.begin(), servers.end(), maxWireCompare);
        return serverDescription->getMaxWireVersion();
    } else {
        return std::numeric_limits<int>::max();
    }
}

std::string ReplicaSetMonitor::getName() const {
    return _uri.getSetName();
}

// TODO: this can be cached
std::string ReplicaSetMonitor::getServerAddress() const {
    const auto topologyDescription = _currentTopology();
    const auto servers = topologyDescription->getServers();

    std::stringstream output;
    output << _uri.getSetName() << "/";

    for (const auto& server : servers) {
        output << server->getAddress();
        if (&server != &servers.back())
            output << ",";
    }

    auto result = output.str();
    return output.str();
}

const MongoURI& ReplicaSetMonitor::getOriginalUri() const {
    return _uri;
}

bool ReplicaSetMonitor::contains(const HostAndPort& host) const {
    return _currentTopology()->findServerByAddress(host.toString()) != boost::none;
}

shared_ptr<ReplicaSetMonitor> ReplicaSetMonitor::createIfNeeded(const string& name,
                                                                const set<HostAndPort>& servers) {
    return globalRSMonitorManager.getOrCreateMonitor(
        ConnectionString::forReplicaSet(name, vector<HostAndPort>(servers.begin(), servers.end())));
}

shared_ptr<ReplicaSetMonitor> ReplicaSetMonitor::createIfNeeded(const MongoURI& uri) {
    return globalRSMonitorManager.getOrCreateMonitor(uri);
}

shared_ptr<ReplicaSetMonitor> ReplicaSetMonitor::get(const std::string& name) {
    return globalRSMonitorManager.getMonitor(name);
}

void ReplicaSetMonitor::remove(const string& name) {
    globalRSMonitorManager.removeMonitor(name);

    // Kill all pooled ReplicaSetConnections for this set. They will not function correctly
    // after we kill the ReplicaSetMonitor.
    globalConnPool.removeHost(name);
}

ReplicaSetChangeNotifier& ReplicaSetMonitor::getNotifier() {
    return globalRSMonitorManager.getNotifier();
}

int32_t pingTimeMillis(const ServerDescriptionPtr& serverDescription) {
    static const Milliseconds::rep maxLatency = numeric_limits<int32_t>::max();
    auto latencyMillis =
        duration_cast<Milliseconds>(serverDescription->getRtt().value_or(Milliseconds(maxLatency)))
            .count();
    return std::min(latencyMillis, maxLatency);
}

void ReplicaSetMonitor::appendInfo(BSONObjBuilder& bsonObjBuilder, bool forFTDC) const {
    auto topologyDescription = _currentTopology();

    BSONObjBuilder monitorInfo(bsonObjBuilder.subobjStart(getName()));
    if (forFTDC) {
        for (auto serverDescription : topologyDescription->getServers()) {
            monitorInfo.appendNumber(serverDescription->getAddress(),
                                     pingTimeMillis(serverDescription));
        }
        return;
    }

    // NOTE: the format here must be consistent for backwards compatibility
    BSONArrayBuilder hosts(monitorInfo.subarrayStart("hosts"));
    for (auto serverDescription : topologyDescription->getServers()) {
        bool isUp = serverDescription->getType() != ServerType::kUnknown;
        bool isMaster = serverDescription->getPrimary() == serverDescription->getAddress();

        BSONObjBuilder builder;
        builder.append("addr", serverDescription->getAddress());
        builder.append("ok", isUp);            // TODO: check what defines up
        builder.append("ismaster", isMaster);  // intentionally not camelCase
        builder.append("hidden", false);       // we don't keep hidden nodes in the
        builder.append("secondary", isUp && isMaster);
        builder.append("pingTimeMillis", pingTimeMillis(serverDescription));

        auto tags = serverDescription->getTags();
        if (tags.size() > 0) {
            builder.append("tags", BSONObj());  // TODO: implement getBsonTags on ServerDescription
        }

        hosts.append(builder.obj());
    }
}

void ReplicaSetMonitor::shutdown() {
    globalRSMonitorManager.shutdown();
}

bool ReplicaSetMonitor::isKnownToHaveGoodPrimary() const {
    return _currentPrimary() != boost::none;
}

sdam::TopologyDescriptionPtr ReplicaSetMonitor::_currentTopology() const {
    return _topologyManager->getTopologyDescription();
}

void ReplicaSetMonitor::onTopologyDescriptionChangedEvent(
    UUID topologyId,
    TopologyDescriptionPtr previousDescription,
    TopologyDescriptionPtr newDescription) {

    if (_hasMembershipChange(previousDescription, newDescription)) {
        // TODO: remove when HostAndPort conversion is done.
        std::vector<HostAndPort> servers;
        for (const auto server : newDescription->getServers()) {
            servers.push_back(HostAndPort(server->getAddress()));
        }

        auto connectionString = ConnectionString::forReplicaSet(getName(), servers);
        auto maybePrimary = newDescription->getPrimary();
        if (maybePrimary) {
            // TODO: remove need for HostAndPort conversion
            std::set<HostAndPort> secondaries;
            for (auto secondary : newDescription->findServers(secondaryPredicate)) {
                secondaries.insert(HostAndPort(secondary->getAddress()));
            }

            auto primaryAddress = HostAndPort((*maybePrimary)->getAddress());
            globalRSMonitorManager.getNotifier().onConfirmedSet(
                connectionString, primaryAddress, secondaries);
        } else {
            globalRSMonitorManager.getNotifier().onPossibleSet(connectionString);
        }
    } else {
        //        _logDebug() << "No membership change for:\nprevious - " <<
        //        previousDescription->toString()
        //              << "\nnew -" << newDescription->toString();
    }
}  // namespace mongo

void ReplicaSetMonitor::onServerHeartbeatSucceededEvent(mongo::Milliseconds durationMs,
                                                        ServerAddress hostAndPort,
                                                        const BSONObj reply) {
    IsMasterOutcome outcome(hostAndPort, reply, durationMs);
    _topologyManager->onServerDescription(outcome);
    _outstandingQueriesCV.notify_all();
}

void ReplicaSetMonitor::onServerPingFailedEvent(const ServerAddress hostAndPort,
                                                const Status& status) {
    failedHost(HostAndPort(hostAndPort), status);
}

void ReplicaSetMonitor::onServerPingSucceededEvent(mongo::Milliseconds durationMS,
                                                   ServerAddress hostAndPort) {
    _topologyManager->onServerRTTUpdated(hostAndPort, durationMS);
}

std::shared_ptr<executor::TaskExecutor> ReplicaSetMonitor::_initTaskExecutor(
    std::shared_ptr<executor::TaskExecutor> executor) {
    if (executor) {
        return executor;
    }

    auto hookList = std::make_unique<rpc::EgressMetadataHookList>();
    auto net = executor::makeNetworkInterface(
        "ReplicaSetMonitor-TaskExecutor", nullptr, std::move(hookList));
    auto pool = std::make_unique<executor::NetworkInterfaceThreadPool>(net.get());
    _executor = std::make_shared<executor::ThreadPoolTaskExecutor>(std::move(pool), std::move(net));
    _executor->startup();
    return _executor;
}

// TODO: remove
std::string debug(boost::optional<std::vector<HostAndPort>> x) {
    std::stringstream s;
    if (x) {
        auto v = *x;
        for (auto h : v) {
            s << h.toString() << "; ";
        }
    }
    return s.str();
}

// TODO: remove
std::string debug(boost::optional<HostAndPort> x) {
    std::stringstream s;
    if (x) {
        auto v = *x;
        s << v.toString() << "; ";
    }
    return s.str();
}

// This function attempts to satisfy outstanding queries when we have new information.
// It also will remove any queries that have already passed the deadline.
//
// It should be called with the mutex held.
//
// TODO: refactor so that we don't call _getHost(s) for every outstanding query
// since some might be duplicates. but we do still want the randomization for multiple hosts.
void ReplicaSetMonitor::_satisfyOutstandingQueries() {
    auto topologyDescription = _currentTopology();
    auto& outstandingQueries = _outstandingQueries;
    //    size_t totalQueries = _outstandingQueries.size();
    size_t numSatisfied = 0;

    bool shouldRemove;
    auto it = outstandingQueries.begin();
    while (it != outstandingQueries.end()) {
        auto& query = *it;
        shouldRemove = false;

        if (query->done) {
            shouldRemove = true;
        } else {
            if (query->type == HostQueryType::MULTI) {
                auto multiQuery = std::static_pointer_cast<MultiHostQuery>(query);
                auto multiResult = _getHosts(multiQuery->criteria);
                if (multiResult) {
                    _executor->cancel(multiQuery->deadlineHandle);

                    multiQuery->done = true;
                    multiQuery->promise.emplaceValue(std::move(*multiResult));
                    _logDebug() << "satisfy multi query: " << multiQuery->criteria.toString()
                                << " (" << _executor->now() - multiQuery->start << ")";
                    shouldRemove = true;
                    ++numSatisfied;
                }
            } else {
                invariant(query->type == HostQueryType::SINGLE);
                auto singleQuery = std::dynamic_pointer_cast<SingleHostQuery>(query);
                invariant(singleQuery);
                auto singleResult = _getHost(singleQuery->criteria);
                if (singleResult) {
                    _executor->cancel(singleQuery->deadlineHandle);

                    singleQuery->done = true;
                    singleQuery->promise.emplaceValue(std::move(*singleResult));
                    _logDebug() << "satisfy single query: " << singleQuery->criteria.toString()
                                << " (" << _executor->now() - singleQuery->start << ")";
                    shouldRemove = true;
                    ++numSatisfied;
                }
            }  // end if
        }      // end if

        if (shouldRemove) {
            it = outstandingQueries.erase(it);
        } else {
            ++it;
        }
    }  // end while

    //    _logDebug() << numSatisfied << " of " << totalQueries << " queries satisfied.";
}

void ReplicaSetMonitor::_startOutstandingQueryProcessor() {
    auto queryProcessor = [self = shared_from_this()]() {
        while (true) {
            stdx::unique_lock<Mutex> lock(self->_mutex);
            Timer timer;
            timer.reset();

            self->_outstandingQueriesCV.wait(lock, [self] {
                auto currentTopology = self->_currentTopology();
                const auto& outstandingQueries = self->_outstandingQueries;
                const auto isClosed = self->_isClosed;

                // only wake up if we are closed or we have an outstanding query that we can
                // satisfy.
                bool isPossibleToSatisfy = outstandingQueries.size() > 0 &&
                    (currentTopology->getType() != TopologyType::kUnknown);
                return isClosed || isPossibleToSatisfy;
            });
            // mutex is reacquired after wait

            //            self->_logDebug() << "waited for " << timer.elapsed() << ". "
            //                              << toString(self->_currentTopology()->getType());

            if (self->_isClosed) {
                self->_logDebug() << "exiting query processor loop";
                return;
            }

            self->_satisfyOutstandingQueries();
        }
    };

    _queryProcessorThread = std::thread(queryProcessor);
    //    _queryProcessorThread.detach();
}

logger::LogstreamBuilder ReplicaSetMonitor::_logDebug(int n) {
    return std::move(log() << kLogPrefix << " (" << this->getName() << ") ");
}

void ReplicaSetMonitor::_failOutstandingWitStatus(Status status) {
    for (auto query : _outstandingQueries) {
        if (query->done)
            continue;

        query->done = true;
        _executor->cancel(query->deadlineHandle);

        switch (query->type) {
            case HostQueryType::SINGLE: {
                auto singleQuery = std::static_pointer_cast<SingleHostQuery>(query);
                singleQuery->promise.setError(status);
                break;
            }
            case HostQueryType::MULTI: {
                auto multiQuery = std::static_pointer_cast<MultiHostQuery>(query);
                multiQuery->promise.setError(status);
                break;
            }
            default:
                MONGO_UNREACHABLE;
        }
    }
    _outstandingQueries.clear();
}

bool ReplicaSetMonitor::_hasMembershipChange(sdam::TopologyDescriptionPtr oldDescription,
                                             sdam::TopologyDescriptionPtr newDescription) {

    // check if primaries differ
    auto maybeOldPrimary = oldDescription->getPrimary();
    auto maybeNewPrimary = newDescription->getPrimary();
    if (maybeNewPrimary && maybeOldPrimary) {
        bool addressesDoNotMatch =
            (*maybeOldPrimary)->getAddress() != (*maybeNewPrimary)->getAddress();
        if (addressesDoNotMatch)
            return true;
    } else {
        // if old or new has a value, then there has been a change.
        bool oneExists = (maybeOldPrimary || maybeNewPrimary);
        if (oneExists)
            return true;
    }

    // check if secondaries differ
    auto oldSecondaries = oldDescription->findServers(secondaryPredicate);
    auto newSecondaries = newDescription->findServers(secondaryPredicate);
    std::unordered_map<ServerAddress, bool> oldMembership;
    for (auto oldSecondary : oldSecondaries) {
        oldMembership.insert({oldSecondary->getAddress(), true});
    }
    for (auto newSecondary : newSecondaries) {
        if (oldMembership.find(newSecondary->getAddress()) == oldMembership.end()) {
            return true;
        }
    }

    return false;
}

Status ReplicaSetMonitor::_makeUnsatisfiedReadPrefError(
    const ReadPreferenceSetting& criteria) const {
    return Status(ErrorCodes::FailedToSatisfyReadPreference,
                  str::stream() << "Could not find host matching read preference "
                                << criteria.toString() << " for set " << getName());
}
}  // namespace mongo
