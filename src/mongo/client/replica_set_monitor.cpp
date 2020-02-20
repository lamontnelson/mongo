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

#include "mongo/bson/simple_bsonelement_comparator.h"
#include "mongo/client/connpool.h"
#include "mongo/client/global_conn_pool.h"
#include "mongo/client/read_preference.h"
#include "mongo/client/replica_set_monitor_query_processor.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/bson_extract_optime.h"
#include "mongo/db/server_options.h"
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/platform/mutex.h"
#include "mongo/rpc/metadata/egress_metadata_hook_list.h"
#include "mongo/stdx/condition_variable.h"
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

namespace {
// Pull nested types to top-level scope
using executor::TaskExecutor;
using CallbackArgs = TaskExecutor::CallbackArgs;
using CallbackHandle = TaskExecutor::CallbackHandle;

const ReadPreferenceSetting kPrimaryOnlyReadPreference(ReadPreference::PrimaryOnly, TagSet());

// Utility functions to use when finding servers
bool minWireCompare(const ServerDescriptionPtr& a, const ServerDescriptionPtr& b) {
    return a->getMinWireVersion() < b->getMinWireVersion();
}

bool maxWireCompare(const ServerDescriptionPtr& a, const ServerDescriptionPtr& b) {
    return a->getMaxWireVersion() < b->getMaxWireVersion();
}

bool secondaryPredicate(const ServerDescriptionPtr& server) {
    return server->getType() == ServerType::kRSSecondary;
}

std::string readPrefToStringWithMinOpTime(const ReadPreferenceSetting& readPref) {
    BSONObjBuilder builder;
    readPref.toInnerBSON(&builder);
    if (!readPref.minOpTime.isNull()) {
        builder.append("minOpTime", readPref.minOpTime.toBSON());
    }
    return builder.obj().toString();
}

// TODO: remove
std::string hostListToString(boost::optional<std::vector<HostAndPort>> x) {
    std::stringstream s;
    if (x) {
        for (auto h : *x) {
            s << h.toString() << "; ";
        }
    }
    return s.str();
}

int32_t pingTimeMillis(const ServerDescriptionPtr& serverDescription) {
    static const Milliseconds maxLatency = Milliseconds::max();
    const auto& serverRtt = serverDescription->getRtt();
    auto latencyMillis = (serverRtt) ? duration_cast<Milliseconds>(*serverRtt) : maxLatency;
    return std::min(latencyMillis, maxLatency).count();
}

constexpr auto kZeroMs = Milliseconds(0);
}  // namespace

ReplicaSetMonitor::ReplicaSetMonitor(const MongoURI& uri, std::shared_ptr<TaskExecutor> executor)
    : _serverSelector(std::make_unique<SdamServerSelector>(kServerSelectionConfig)),
      _queryProcessor(std::make_shared<ReplicaSetMonitorQueryProcessor>()),
      _uri(uri),
      _executor(executor),
      _random(PseudoRandom(SecureRandom().nextInt64())) {

    // TODO: sdam should use the HostAndPort type for ServerAddress
    std::vector<ServerAddress> seeds;
    for (const auto& seed : uri.getServers()) {
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
    stdx::lock_guard lock(_mutex);
    LOG(kDefaultLogLevel) << _logPrefix() << "Starting Replica Set Monitor with uri: " << _uri;

    _eventsPublisher = std::make_shared<sdam::TopologyEventsPublisher>(_executor);
    _topologyManager =
        std::make_unique<TopologyManager>(_sdamConfig, _clockSource, _eventsPublisher);
    _isMasterMonitor = std::make_unique<ServerIsMasterMonitor>(
        _uri, _sdamConfig, _eventsPublisher, _topologyManager->getTopologyDescription(), _executor);

    _eventsPublisher->registerListener(shared_from_this());
    _eventsPublisher->registerListener(_isMasterMonitor);
    _isDropped.store(false);

    globalRSMonitorManager.getNotifier().onFoundSet(getName());
}

void ReplicaSetMonitor::drop() {
    stdx::lock_guard lock(_mutex);
    if (_isDropped.load())
        return;

    _isDropped.store(true);
    LOG(kDefaultLogLevel) << _logPrefix() << "Closing Replica Set Monitor";
    _eventsPublisher->close();
    _queryProcessor->shutdown();
    _isMasterMonitor->shutdown();
    _failOutstandingWithStatus(
        lock, Status{ErrorCodes::ShutdownInProgress, "the ReplicaSetMonitor is shutting down"});

    globalRSMonitorManager.getNotifier().onDroppedSet(getName());

    LOG(kDefaultLogLevel) << _logPrefix() << "Done closing Replica Set Monitor";
}

SemiFuture<HostAndPort> ReplicaSetMonitor::getHostOrRefresh(const ReadPreferenceSetting& criteria,
                                                            Milliseconds maxWait) {
    return getHostsOrRefresh(criteria, maxWait)
        .thenRunOn(_executor)
        .then([self = shared_from_this()](const std::vector<HostAndPort>& result) {
            invariant(result.size());
            return result[self->_random.nextInt64(result.size())];
        })
        .semi();
}

std::vector<HostAndPort> ReplicaSetMonitor::_extractHosts(
    const std::vector<ServerDescriptionPtr>& serverDescriptions) {
    std::vector<HostAndPort> result;
    for (const auto& server : serverDescriptions) {
        result.push_back(HostAndPort(server->getAddress()));
    }
    return result;
}

SemiFuture<std::vector<HostAndPort>> ReplicaSetMonitor::getHostsOrRefresh(
    const ReadPreferenceSetting& criteria, Milliseconds maxWait) {
    // In the fast case (stable topology), we avoid mutex acquisition.
    if (_isDropped.load()) {
        return _makeReplicaSetMonitorRemovedError();
    }

    // start counting from the beginning of the operation
    const auto deadline = _executor->now() + ((maxWait > kZeroMs) ? maxWait : kZeroMs);

    // try to satisfy query immediately
    auto immediateResult = _getHosts(criteria);
    if (immediateResult) {
        LOG(kLowerLogLevel) << _logPrefix()
                            << "getHosts: " << readPrefToStringWithMinOpTime(criteria) << " -> "
                            << hostListToString(immediateResult);
        return {*immediateResult};
    }

    _isMasterMonitor->requestImmediateCheck();
    LOG(kDefaultLogLevel) << _logPrefix()
                          << "start getHosts: " << readPrefToStringWithMinOpTime(criteria);

    // fail fast on timeout
    const Date_t& now = _executor->now();
    if (deadline <= now) {
        return _makeUnsatisfiedReadPrefError(criteria);
    }

    {
        stdx::lock_guard lk(_mutex);

        // We check if we are closed under the mutex here since someone could have called
        // close() concurrently with the code above.
        if (_isDropped.load()) {
            return _makeReplicaSetMonitorRemovedError();
        }

        return _enqueueOutstandingQuery(lk, criteria, deadline);
    }
}

SemiFuture<std::vector<HostAndPort>> ReplicaSetMonitor::_enqueueOutstandingQuery(
    WithLock, const ReadPreferenceSetting& criteria, const Date_t& deadline) {
    using HostAndPortList = std::vector<HostAndPort>;
    Future<HostAndPortList> result;

    auto query = std::make_shared<HostQuery>();
    query->criteria = criteria;
    query->deadline = deadline;

    auto pf = makePromiseFuture<HostAndPortList>();
    query->promise = std::move(pf.promise);

    auto deadlineCb =
        [this, query, self = shared_from_this()](const TaskExecutor::CallbackArgs& cbArgs) {
            stdx::lock_guard lock(_mutex);
            if (query->done) {
                return;
            }

            const auto cbStatus = cbArgs.status;
            if (!cbStatus.isOK()) {
                query->promise.setError(cbStatus);
                query->done = true;
                return;
            }

            const auto errorStatus = _makeUnsatisfiedReadPrefError(query->criteria);
            query->promise.setError(errorStatus);
            query->done = true;
            LOG(kDefaultLogLevel) << _logPrefix()
                                  << "host selection timeout: " << errorStatus.toString();
        };
    auto swDeadlineHandle = _executor->scheduleWorkAt(query->deadline, deadlineCb);

    if (!swDeadlineHandle.isOK()) {
        log() << "error scheduling deadline handler: " << swDeadlineHandle.getStatus();
        return SemiFuture<HostAndPortList>::makeReady(swDeadlineHandle.getStatus());
    }
    query->deadlineHandle = swDeadlineHandle.getValue();
    _outstandingQueries.push_back(query);

    // Send topology changes to the query processor to satisfy the future.
    // It will be removed as a listener when all waiting queries have been satisfied.
    _eventsPublisher->registerListener(_queryProcessor);

    return std::move(pf.future).semi();
}

boost::optional<std::vector<HostAndPort>> ReplicaSetMonitor::_getHosts(
    const TopologyDescriptionPtr& topology, const ReadPreferenceSetting& criteria) {
    auto result = _serverSelector->selectServers(topology, criteria);
    if (!result)
        return boost::none;
    return _extractHosts(*result);
}

boost::optional<std::vector<HostAndPort>> ReplicaSetMonitor::_getHosts(
    const ReadPreferenceSetting& criteria) {
    return _getHosts(_currentTopology(), criteria);
}

HostAndPort ReplicaSetMonitor::getMasterOrUassert() {
    return getHostOrRefresh(kPrimaryOnlyReadPreference).get();
}

void ReplicaSetMonitor::failedHost(const HostAndPort& host, const Status& status) {
    failedHost(host, BSONObj(), status);
}

void ReplicaSetMonitor::failedHost(const HostAndPort& host, BSONObj bson, const Status& status) {
    IsMasterOutcome outcome(host.toString(), bson, status.toString());
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
    auto currentTopology = _currentTopology();
    const auto& serverDescription = currentTopology->findServerByAddress(host.toString());
    return serverDescription && (*serverDescription)->getType() != ServerType::kUnknown;
}

int ReplicaSetMonitor::getMinWireVersion() const {
    auto currentTopology = _currentTopology();
    const std::vector<ServerDescriptionPtr>& servers = currentTopology->getServers();
    if (servers.size() > 0) {
        const auto& serverDescription =
            *std::min_element(servers.begin(), servers.end(), minWireCompare);
        return serverDescription->getMinWireVersion();
    } else {
        return 0;
    }
}

int ReplicaSetMonitor::getMaxWireVersion() const {
    auto currentTopology = _currentTopology();
    const std::vector<ServerDescriptionPtr>& servers = currentTopology->getServers();
    if (servers.size() > 0) {
        const auto& serverDescription =
            *std::max_element(servers.begin(), servers.end(), maxWireCompare);
        return serverDescription->getMaxWireVersion();
    } else {
        return std::numeric_limits<int>::max();
    }
}

std::string ReplicaSetMonitor::getName() const {
    return _uri.getSetName();
}

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
    return static_cast<bool>(_currentTopology()->findServerByAddress(host.toString()));
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
    for (const auto& serverDescription : topologyDescription->getServers()) {
        bool isUp = false;
        bool isMaster = false;
        bool isSecondary = false;
        bool isHidden = false;

        switch (serverDescription->getType()) {
            case ServerType::kRSPrimary:
                isUp = true;
                isMaster = true;
                break;
            case ServerType::kRSSecondary:
                isUp = true;
                isSecondary = true;
                break;
            case ServerType::kStandalone:
                isUp = true;
                break;
            case ServerType::kMongos:
                isUp = true;
                break;
            case ServerType::kRSGhost:
                isHidden = true;
                break;
            case ServerType::kRSArbiter:
                isHidden = true;
                break;
            default:
                break;
        }

        BSONObjBuilder builder(hosts.subobjStart());
        builder.append("addr", serverDescription->getAddress());
        builder.append("ok", isUp);
        builder.append("ismaster", isMaster);  // intentionally not camelCase
        builder.append("hidden", isHidden);
        builder.append("secondary", isSecondary);
        builder.append("pingTimeMillis", pingTimeMillis(serverDescription));

        if (serverDescription->getTags().size()) {
            BSONObjBuilder tagsBuilder(builder.subobjStart("tags"));
            serverDescription->appendBsonTags(tagsBuilder);
        }
    }
}

void ReplicaSetMonitor::shutdown() {
    globalRSMonitorManager.shutdown();
}

bool ReplicaSetMonitor::isKnownToHaveGoodPrimary() const {
    return static_cast<bool>(_currentPrimary());
}

sdam::TopologyDescriptionPtr ReplicaSetMonitor::_currentTopology() const {
    return _topologyManager->getTopologyDescription();
}

void ReplicaSetMonitor::onTopologyDescriptionChangedEvent(
    UUID topologyId,
    TopologyDescriptionPtr previousDescription,
    TopologyDescriptionPtr newDescription) {

    // notify external components, if there are membership
    // changes in the topology.
    if (_hasMembershipChange(previousDescription, newDescription)) {
        LOG(kDefaultLogLevel) << _logPrefix() << "Topology Change: " << newDescription->toString();

        // TODO: remove when HostAndPort conversion is done.
        std::vector<HostAndPort> servers = _extractHosts(newDescription->getServers());

        auto connectionString = ConnectionString::forReplicaSet(getName(), servers);
        auto maybePrimary = newDescription->getPrimary();
        if (maybePrimary) {
            // TODO: remove need for HostAndPort conversion
            auto hostList = _extractHosts(newDescription->findServers(secondaryPredicate));
            std::set<HostAndPort> secondaries(hostList.begin(), hostList.end());

            auto primaryAddress = HostAndPort((*maybePrimary)->getAddress());
            globalRSMonitorManager.getNotifier().onConfirmedSet(
                connectionString, primaryAddress, secondaries);
        } else {
            globalRSMonitorManager.getNotifier().onPossibleSet(connectionString);
        }
    }
}

void ReplicaSetMonitor::onServerHeartbeatSucceededEvent(sdam::IsMasterRTT durationMs,
                                                        const ServerAddress& hostAndPort,
                                                        const BSONObj reply) {
    IsMasterOutcome outcome(hostAndPort, reply, durationMs);
    _topologyManager->onServerDescription(outcome);
}

void ReplicaSetMonitor::onServerHeartbeatFailureEvent(IsMasterRTT durationMs,
                                                      Status errorStatus,
                                                      const ServerAddress& hostAndPort,
                                                      const BSONObj reply) {
    IsMasterOutcome outcome(hostAndPort, reply, errorStatus.toString());
    _topologyManager->onServerDescription(outcome);
}

void ReplicaSetMonitor::onServerPingFailedEvent(const ServerAddress& hostAndPort,
                                                const Status& status) {
    failedHost(HostAndPort(hostAndPort), status);
}

void ReplicaSetMonitor::onServerPingSucceededEvent(sdam::IsMasterRTT durationMS,
                                                   const ServerAddress& hostAndPort) {
    _topologyManager->onServerRTTUpdated(hostAndPort, durationMS);
}

std::string ReplicaSetMonitor::_logPrefix() {
    return str::stream() << kLogPrefix << " [" << getName() << "] ";
}

void ReplicaSetMonitor::_failOutstandingWithStatus(WithLock, Status status) {
    for (const auto& query : _outstandingQueries) {
        if (query->done)
            continue;

        query->done = true;
        _executor->cancel(query->deadlineHandle);
        query->promise.setError(status);
    }
    _outstandingQueries.clear();
}

bool ReplicaSetMonitor::_hasMembershipChange(sdam::TopologyDescriptionPtr oldDescription,
                                             sdam::TopologyDescriptionPtr newDescription) {

    if (oldDescription->getServers().size() != newDescription->getServers().size())
        return true;

    for (const auto& server : oldDescription->getServers()) {
        const auto newServer = newDescription->findServerByAddress(server->getAddress());
        if (!newServer)
            return true;
        const ServerDescription& s = *server;
        const ServerDescription& ns = **newServer;
        if (s != ns)
            return true;
    }

    for (const auto& server : newDescription->getServers()) {
        auto oldServer = oldDescription->findServerByAddress(server->getAddress());
        if (!oldServer)
            return true;
    }

    return false;
}

void ReplicaSetMonitor::_processOutstanding(const TopologyDescriptionPtr& topologyDescription) {
    // TODO: refactor so that we don't call _getHost(s) for every outstanding query
    // since there might be duplicates.
    stdx::lock_guard lock(_mutex);

    bool shouldRemove;
    auto it = _outstandingQueries.begin();
    while (it != _outstandingQueries.end()) {
        auto& query = *it;
        shouldRemove = false;

        if (query->done) {
            shouldRemove = true;
        } else {
            auto result = _getHosts(topologyDescription, query->criteria);
            if (result) {
                _executor->cancel(query->deadlineHandle);
                query->done = true;
                query->promise.emplaceValue(std::move(*result));
                LOG(kDefaultLogLevel)
                    << _logPrefix()
                    << "finish getHosts: " << readPrefToStringWithMinOpTime(query->criteria) << " ("
                    << _executor->now() - query->start << ")";
                shouldRemove = true;
            }
        }

        it = (shouldRemove) ? _outstandingQueries.erase(it) : ++it;
    }

    if (_outstandingQueries.size()) {
        // enable expedited mode
        _isMasterMonitor->requestImmediateCheck();
    } else {
        // if no more outstanding queries, no need to listen for topology changes in
        // this monitor.
        _eventsPublisher->removeListener(_queryProcessor);
    }
}

Status ReplicaSetMonitor::_makeUnsatisfiedReadPrefError(
    const ReadPreferenceSetting& criteria) const {
    return Status(ErrorCodes::FailedToSatisfyReadPreference,
                  str::stream() << "Could not find host matching read preference "
                                << criteria.toString() << " for set " << getName());
}

Status ReplicaSetMonitor::_makeReplicaSetMonitorRemovedError() const {
    return Status(ErrorCodes::ReplicaSetMonitorRemoved,
                  str::stream() << "ReplicaSetMonitor for set " << getName() << " is removed");
}
}  // namespace mongo
