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

using TopologyManagerPtr = sdam::TopologyManagerPtr;
using TopologyDescriptionPtr = sdam::TopologyDescriptionPtr;
using ServerDescriptionPtr = sdam::ServerDescriptionPtr;
using ServerType = sdam::ServerType;

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

// If we cannot find a host after 15 seconds of refreshing, give up
const Seconds ReplicaSetMonitor::kDefaultFindHostTimeout(15);

ReplicaSetMonitor::ReplicaSetMonitor(const MongoURI& uri) : _uri(uri) {}

ReplicaSetMonitor::~ReplicaSetMonitor() {}


boost::optional<HostAndPort> ReplicaSetMonitor::getMatchingHost(
    TopologyDescriptionPtr topologyDescription, const ReadPreferenceSetting& criteria) const {
    auto hosts = getMatchingHosts(criteria);
    if (hosts) {
        return (*hosts)[0];
    }
    return boost::none;
}

boost::optional<std::vector<HostAndPort>> ReplicaSetMonitor::getMatchingHosts(
    TopologyDescriptionPtr topologyDescription, const ReadPreferenceSetting& criteria) const {
    switch (criteria.pref) {
        // "Prefered" read preferences are defined in terms of other preferences
        case ReadPreference::PrimaryPreferred: {
            boost::optional<std::vector<HostAndPort>> out =
                getMatchingHosts(ReadPreferenceSetting(ReadPreference::PrimaryOnly, criteria.tags));
            // NOTE: the spec says we should use the primary even if tags don't match
            // TODO: double check this
            if (out)
                return out;
            return getMatchingHosts(ReadPreferenceSetting(
                ReadPreference::SecondaryOnly, criteria.tags, criteria.maxStalenessSeconds));
        }

        case ReadPreference::SecondaryPreferred: {
            boost::optional<std::vector<HostAndPort>> out = getMatchingHosts(ReadPreferenceSetting(
                ReadPreference::SecondaryOnly, criteria.tags, criteria.maxStalenessSeconds));
            if (out)
                return out;
            // NOTE: the spec says we should use the primary even if tags don't match
            // TODO: double check this
            return getMatchingHosts(
                ReadPreferenceSetting(ReadPreference::PrimaryOnly, criteria.tags));
        }

        case ReadPreference::PrimaryOnly: {
            auto primary = _currentPrimary();
            if (primary) {
                return std::vector<HostAndPort>{HostAndPort((*primary)->getAddress())};
            }
            return boost::none;
        }

        case ReadPreference::SecondaryOnly:
        case ReadPreference::Nearest: {
        }
        default:
            uassert(16337, "Unknown read preference", false);
            break;
    }
}


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
    const auto topologyDescription = _topologyManager->getTopologyDescription();
    topologyDescription->findServers(
        [this, &criteria](const ServerDescriptionPtr& serverDescription) {
            auto stalenessCutoff = criteria.maxStalenessSeconds;

            return true;
        });
}

HostAndPort ReplicaSetMonitor::getMasterOrUassert() {
    return getHostOrRefresh(kPrimaryOnlyReadPreference).get();
}

void ReplicaSetMonitor::failedHost(const HostAndPort& host, const Status& status) {}

boost::optional<ServerDescriptionPtr> ReplicaSetMonitor::_currentPrimary() const {
    const auto primaries =
        _topologyManager->getTopologyDescription()->findServers(primaryPredicate);
    invariant(primaries.size() <= 1);  // TODO: check this invariant
    return (primaries.size()) ? boost::optional<ServerDescriptionPtr>(primaries[0]) : boost::none;
}

bool ReplicaSetMonitor::isPrimary(const HostAndPort& host) const {
    const auto currentPrimary = _currentPrimary();
    return (currentPrimary ? (*currentPrimary)->getAddress() == host.toString() : false);
}

bool ReplicaSetMonitor::isHostUp(const HostAndPort& host) const {
    // TODO: check this impl; maybe verify serverType also
    return _topologyManager->getTopologyDescription()->findServerByAddress(host.toString()) !=
        boost::none;
}

int ReplicaSetMonitor::getMinWireVersion() const {
    const std::vector<ServerDescriptionPtr>& servers =
        _topologyManager->getTopologyDescription()->getServers();
    if (servers.size() > 0) {
        const auto serverDescription =
            *std::min_element(servers.begin(), servers.end(), minWireCompare);
        return serverDescription->getMinWireVersion();
    } else {
        return AGG_RETURNS_CURSORS;  // TODO: check this case / MONGO_UNREACHABLE?
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
        return -1;  // TODO: check this case / MONGO_UNREACHABLE?
    }
}

std::string ReplicaSetMonitor::getName() const {
    const auto setName = _topologyManager->getTopologyDescription()->getSetName();
    return (setName) ? *setName : "";
}

std::string ReplicaSetMonitor::getServerAddress() const {
    const auto topologyDescription = _topologyManager->getTopologyDescription();
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
    return _topologyManager->getTopologyDescription()->findServerByAddress(host.toString()) !=
        boost::none;
}

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

void ReplicaSetMonitor::remove(const string& name) {}

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

bool ReplicaSetMonitor::isKnownToHaveGoodPrimary() const {
    return _currentPrimary() != boost::none;
}

void ReplicaSetMonitor::runScanForMockReplicaSet() {}
}  // namespace mongo
