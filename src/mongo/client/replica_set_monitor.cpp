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
#include "mongo/platform/atomic_word.h"
#include "mongo/platform/mutex.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/util/background.h"
#include "mongo/util/debug_util.h"
#include "mongo/util/exit.h"
#include "mongo/util/fail_point.h"
#include "mongo/util/log.h"
#include "mongo/util/string_map.h"
#include "mongo/util/timer.h"

namespace mongo {

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

// Intentionally chosen to compare worse than all known latencies.
const int64_t unknownLatency = numeric_limits<int64_t>::max();

const ReadPreferenceSetting kPrimaryOnlyReadPreference(ReadPreference::PrimaryOnly, TagSet());
const Milliseconds kExpeditedRefreshPeriod(500);


/**
 * Replica set refresh period on the task executor.
 */
const Seconds kDefaultRefreshPeriod(30);
}  // namespace

// If we cannot find a host after 15 seconds of refreshing, give up
const Seconds ReplicaSetMonitor::kDefaultFindHostTimeout(15);

// Defaults to random selection as required by the spec
bool ReplicaSetMonitor::useDeterministicHostSelection = false;

Seconds ReplicaSetMonitor::getDefaultRefreshPeriod() {
    Seconds r = kDefaultRefreshPeriod;
    static constexpr auto kPeriodField = "period"_sd;
    modifyReplicaSetMonitorDefaultRefreshPeriod.executeIf(
        [&r](const BSONObj& data) { r = Seconds{data.getIntField(kPeriodField)}; },
        [](const BSONObj& data) { return data.hasField(kPeriodField); });
    return r;
}


ReplicaSetMonitor::ReplicaSetMonitor(const MongoURI& uri) : _uri(uri) {}

ReplicaSetMonitor::~ReplicaSetMonitor() {}

SemiFuture<HostAndPort> ReplicaSetMonitor::getHostOrRefresh(const ReadPreferenceSetting& criteria,
                                                            Milliseconds maxWait) {
    return _getHostsOrRefresh(criteria, maxWait)
        .then([](const auto& hosts) {
            invariant(hosts.size());
            return hosts[0];
        })
        .semi();
}

SemiFuture<std::vector<HostAndPort>> ReplicaSetMonitor::getHostsOrRefresh(
    const ReadPreferenceSetting& criteria, Milliseconds maxWait) {
    return _getHostsOrRefresh(criteria, maxWait).semi();
}

Future<std::vector<HostAndPort>> ReplicaSetMonitor::_getHostsOrRefresh(
    const ReadPreferenceSetting& criteria, Milliseconds maxWait) {
    return Future<std::vector<HostAndPort>>({});
}

HostAndPort ReplicaSetMonitor::getMasterOrUassert() {
    return getHostOrRefresh(kPrimaryOnlyReadPreference).get();
}

void ReplicaSetMonitor::failedHost(const HostAndPort& host, const Status& status) {}

bool ReplicaSetMonitor::isPrimary(const HostAndPort& host) const {
    return true;
}

bool ReplicaSetMonitor::isHostUp(const HostAndPort& host) const {
    return true;
}

int ReplicaSetMonitor::getMinWireVersion() const {
    return 0;
}

int ReplicaSetMonitor::getMaxWireVersion() const {
    return 0;
}

std::string ReplicaSetMonitor::getName() const {
    return "";
}

std::string ReplicaSetMonitor::getServerAddress() const {
    return "";
}

const MongoURI& ReplicaSetMonitor::getOriginalUri() const { return _uri; }

bool ReplicaSetMonitor::contains(const HostAndPort& host) const { return true; }

shared_ptr<ReplicaSetMonitor> ReplicaSetMonitor::createIfNeeded(const string& name,
                                                                const set<HostAndPort>& servers) {
    //    return globalRSMonitorManager.getOrCreateMonitor(
    //        ConnectionString::forReplicaSet(name, vector<HostAndPort>(servers.begin(),
    //        servers.end())));
    return nullptr;
}

shared_ptr<ReplicaSetMonitor> ReplicaSetMonitor::createIfNeeded(const MongoURI& uri) {
    return nullptr;
}

shared_ptr<ReplicaSetMonitor> ReplicaSetMonitor::get(const std::string& name) {
    return nullptr;
}

void ReplicaSetMonitor::remove(const string& name) {
    //    globalRSMonitorManager.removeMonitor(name);
    //    globalConnPool.removeHost(name);
}

ReplicaSetChangeNotifier& ReplicaSetMonitor::getNotifier() {
    return globalRSMonitorManager.getNotifier();
}

void ReplicaSetMonitor::appendInfo(BSONObjBuilder& bsonObjBuilder, bool forFTDC) const {
    //    stdx::lock_guard<Latch> lk(_state->mutex);
    //    BSONObjBuilder monitorInfo(bsonObjBuilder.subobjStart(getName()));
    //    if (forFTDC) {
    //        for (size_t i = 0; i < _state->nodes.size(); i++) {
    //            const Node& node = _state->nodes[i];
    //            monitorInfo.appendNumber(node.host.toString(), pingTimeMillis(node));
    //        }
    //        return;
    //    }
    //
    //    // NOTE: the format here must be consistent for backwards compatibility
    //    BSONArrayBuilder hosts(monitorInfo.subarrayStart("hosts"));
    //    for (size_t i = 0; i < _state->nodes.size(); i++) {
    //        const Node& node = _state->nodes[i];
    //
    //        BSONObjBuilder builder;
    //        builder.append("addr", node.host.toString());
    //        builder.append("ok", node.isUp);
    //        builder.append("ismaster", node.isMaster);  // intentionally not camelCase
    //        builder.append("hidden", false);            // we don't keep hidden nodes in the set
    //        builder.append("secondary", node.isUp && !node.isMaster);
    //        builder.append("pingTimeMillis", pingTimeMillis(node));
    //
    //        if (!node.tags.isEmpty()) {
    //            builder.append("tags", node.tags);
    //        }
    //
    //        hosts.append(builder.obj());
    //    }
}

void ReplicaSetMonitor::shutdown() {
    //    globalRSMonitorManager.shutdown();
}

void ReplicaSetMonitor::cleanup() {
    //    globalRSMonitorManager.removeAllMonitors();
}

void ReplicaSetMonitor::disableRefreshRetries_forTest() {}

bool ReplicaSetMonitor::isKnownToHaveGoodPrimary() const { return true; }

void ReplicaSetMonitor::runScanForMockReplicaSet() {
}

void ScanState::markHostsToScanAsTried() noexcept {
    while (!hostsToScan.empty()) {
        auto host = hostsToScan.front();
        hostsToScan.pop_front();
        /**
         * Mark the popped host as tried to avoid deleting hosts in multiple points.
         * This emulates the final effect of Refresher::getNextStep() on the set.
         */
        triedHosts.insert(host);
    }
}
}  // namespace mongo
