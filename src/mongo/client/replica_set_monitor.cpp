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
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/platform/mutex.h"
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

static auto primaryPredicate = [](const ServerDescriptionPtr& server) {
    return server->getType() == ServerType::kRSPrimary;
};
}  // namespace

ReplicaSetMonitor::ReplicaSetMonitor(const MongoURI& uri)
    : _serverSelector(std::make_unique<SdamServerSelector>(SERVER_SELECTION_CONFIG)),
      _isMasterMonitor(std::make_unique<ServerIsMasterMonitor>(globalRSMonitorManager.getExecutor())),
      _taskExecutor(globalRSMonitorManager.getExecutor()),
      _uri(uri) {
    // TODO: sdam should use the HostAndPort type for ServerAddress
    std::vector<ServerAddress> seeds;
    for (const auto seed : uri.getServers()) {
        seeds.push_back(seed.toString());
    }
    _sdamConfig = SdamConfiguration(seeds);
    _clockSource = getGlobalServiceContext()->getPreciseClockSource();
    _topologyManager = std::make_unique<TopologyManager>(_sdamConfig, _clockSource);
}

ReplicaSetMonitor::~ReplicaSetMonitor() {}

SemiFuture<HostAndPort> ReplicaSetMonitor::getHostOrRefresh(const ReadPreferenceSetting& criteria,
                                                            Milliseconds maxWait) {
    const auto deadline = _now() + maxWait;

    // try to satisfy query immediately
    const auto& immediateResult = _getHost(criteria);
    if (immediateResult) {
        return {std::move(*immediateResult)};
    }

    // fail fast on timeout
    if (deadline - _now() < Milliseconds(0)) {
        return SemiFuture<HostAndPort>(
            Status(ErrorCodes::FailedToSatisfyReadPreference, "failed to satisfy read preference"));
    }

    // otherwise, do async version
    // TODO: refactor this
    mongo::stdx::lock_guard<Mutex> lk(_mutex);
    auto pf = makePromiseFuture<HostAndPort>();
    auto query = std::make_shared<SingleHostQuery>();
    query->type = HostQueryType::SINGLE;
    query->deadline = deadline;
    query->criteria = criteria;
    query->promise = std::move(pf.promise);
    _outstandingQueries.emplace_back(query);
    return std::move(pf.future).semi();
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

Date_t ReplicaSetMonitor::_now() {
    // TODO: executor has a notion of time which we should probably use
    // putting this function here for now to provide a level of indirection
    return _clockSource->now();
}

SemiFuture<std::vector<HostAndPort>> ReplicaSetMonitor::getHostsOrRefresh(
    const ReadPreferenceSetting& criteria, Milliseconds maxWait) {
    const auto deadline = _now() + maxWait;

    // try to satisfy query immediately
    const auto& immediateResult = _getHosts(criteria);
    if (immediateResult) {
        return {std::move(*immediateResult)};
    }

    // fail fast on timeout
    if (deadline - _now() < Milliseconds(0)) {
        return SemiFuture<std::vector<HostAndPort>>(
            Status(ErrorCodes::FailedToSatisfyReadPreference, "failed to satisfy read preference"));
    }

    // otherwise, do async version
    // TODO: refactor this
    mongo::stdx::lock_guard<Mutex> lk(_mutex);
    auto pf = makePromiseFuture<std::vector<HostAndPort>>();
    auto query = std::make_shared<MultiHostQuery>();
    query->type = HostQueryType::MULTI;
    query->deadline = deadline;
    query->criteria = criteria;
    query->promise = std::move(pf.promise);
    _outstandingQueries.emplace_back(query);
    return std::move(pf.future).semi();
}

boost::optional<std::vector<HostAndPort>> ReplicaSetMonitor::_getHosts(
    const ReadPreferenceSetting& criteria) {
    auto result = _serverSelector->selectServers(_currentTopology(), criteria);
    if (result) {
        return _extractHosts(*result);
    }
    return boost::none;
}

boost::optional<HostAndPort> ReplicaSetMonitor::_getHost(const ReadPreferenceSetting& criteria) {
    auto result = _serverSelector->selectServer(_currentTopology(), criteria);
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
    const auto primaries = _currentTopology()->findServers(primaryPredicate);
    return (primaries.size()) ? boost::optional<ServerDescriptionPtr>(primaries[0]) : boost::none;
}

bool ReplicaSetMonitor::isPrimary(const HostAndPort& host) const {
    const auto currentPrimary = _currentPrimary();
    return (currentPrimary ? (*currentPrimary)->getAddress() == host.toString() : false);
}

bool ReplicaSetMonitor::isHostUp(const HostAndPort& host) const {
    const boost::optional<ServerDescriptionPtr>& serverDescription =
        _currentTopology()->findServerByAddress(host.toString());
    return serverDescription != boost::none && (*serverDescription)->getType() != ServerType::kUnknown;
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
    const auto setName = _currentTopology()->getSetName();
    return (setName) ? *setName : "";
}

std::string ReplicaSetMonitor::getServerAddress() const {
    const auto topologyDescription = _currentTopology();
    const auto setName = topologyDescription->getSetName();
    const auto servers = topologyDescription->getServers();

    std::stringstream output;
    if (setName) {
        output << *setName << "/";
    }

    for (const auto& server : servers) {
        output << server->getAddress();
        if (&server != &servers.back())
            output << ",";
    }

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

void ReplicaSetMonitor::appendInfo(BSONObjBuilder& bsonObjBuilder, bool forFTDC) const {
    //        TODO: implement
    //        stdx::lock_guard<Latch> lk(_state->mutex);
    //        BSONObjBuilder monitorInfo(bsonObjBuilder.subobjStart(getName()));
    //        if (forFTDC) {
    //            for (size_t i = 0; i < _state->nodes.size(); i++) {
    //                const Node& node = _state->nodes[i];
    //                monitorInfo.appendNumber(node.host.toString(), pingTimeMillis(node));
    //            }
    //            return;
    //        }
    //
    //        // NOTE: the format here must be consistent for backwards compatibility
    //        BSONArrayBuilder hosts(monitorInfo.subarrayStart("hosts"));
    //        for (size_t i = 0; i < _state->nodes.size(); i++) {
    //            const Node& node = _state->nodes[i];
    //
    //            BSONObjBuilder builder;
    //            builder.append("addr", node.host.toString());
    //            builder.append("ok", node.isUp);
    //            builder.append("ismaster", node.isMaster);  // intentionally not camelCase
    //            builder.append("hidden", false);            // we don't keep hidden nodes in the
    //            set builder.append("secondary", node.isUp && !node.isMaster);
    //            builder.append("pingTimeMillis", pingTimeMillis(node));
    //
    //            if (!node.tags.isEmpty()) {
    //                builder.append("tags", node.tags);
    //            }
    //
    //            hosts.append(builder.obj());
    //        }
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
}

void ReplicaSetMonitor::onServerHeartbeatSucceededEvent(mongo::Milliseconds durationMs,
                                                        ServerAddress hostAndPort) {

}

void ReplicaSetMonitor::onServerPingFailedEvent(const ServerAddress hostAndPort,
                                                const Status& status) {
    failedHost(HostAndPort(hostAndPort), status);
}

void ReplicaSetMonitor::onServerPingSucceededEvent(mongo::Milliseconds durationMS,
                                                   ServerAddress hostAndPort) {
    _topologyManager->onServerRTTUpdated(hostAndPort, durationMS);
}
}  // namespace mongo
