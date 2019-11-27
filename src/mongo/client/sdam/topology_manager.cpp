/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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
#include "mongo/client/sdam/topology_manager.h"

#include "mongo/client/sdam/topology_state_machine.h"

namespace mongo::sdam {

TopologyManager::TopologyManager(SdamConfiguration config, ClockSource* clockSource)
    : _config(std::move(config)),
      _clockSource(clockSource),
      _topologyDescription(std::make_unique<TopologyDescription>(_config)),
      _topologyStateMachine(std::make_unique<TopologyStateMachine>(_config)) {}

void TopologyManager::onServerDescription(const IsMasterOutcome& isMasterOutcome) {
    stdx::lock_guard<mongo::Mutex> lock(_mutex);

    const auto& lastServerDescription =
        _topologyDescription->findServerByAddress(isMasterOutcome.getServer());
    boost::optional<IsMasterRTT> lastRTT =
        (lastServerDescription) ? (*lastServerDescription)->getRtt() : boost::none;

    auto newServerDescription =
        std::make_shared<ServerDescription>(_clockSource, isMasterOutcome, lastRTT);

    auto newTopologyDescription = std::make_unique<TopologyDescription>(*_topologyDescription);
    _topologyStateMachine->onServerDescription(*newTopologyDescription, newServerDescription);
    _topologyDescription = std::move(newTopologyDescription);
}

const std::shared_ptr<TopologyDescription> TopologyManager::getTopologyDescription() const {
    stdx::lock_guard<mongo::Mutex> lock(_mutex);
    return _topologyDescription;
}

/////////////////////////////////
// Replica Set Monitor Interface
/////////////////////////////////

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


/**
 * Schedules the initial refresh task into task executor.
 */
void TopologyManager::init(){
    // TODO: probably don't need this
};

/**
 * Ends any ongoing refreshes.
 */
void TopologyManager::drop(){
    // TODO: probably don't need this
};

/**
 * Returns a host matching the given read preference or an error, if no host matches.
 *
 * @param readPref Read preference to match against
 * @param maxWait If no host is readily available, which matches the specified read preference,
 *   wait for one to become available for up to the specified time and periodically refresh
 *   the view of the set. The call may return with an error earlier than the specified value,
 *   if none of the known hosts for the set are reachable within some number of attempts.
 *   Note that if a maxWait of 0ms is specified, this method may still attempt to contact
 *   every host in the replica set up to one time.
 *
 * Known errors are:
 *  FailedToSatisfyReadPreference, if node cannot be found, which matches the read preference.
 */
SemiFuture<HostAndPort> TopologyManager::getHostOrRefresh(const ReadPreferenceSetting& readPref,
                                                          Milliseconds maxWait) {
    // TODO
    return SemiFuture<HostAndPort>();
};

SemiFuture<std::vector<HostAndPort>> TopologyManager::getHostsOrRefresh(
    const ReadPreferenceSetting& readPref, Milliseconds maxWait) {
    // TODO
    return SemiFuture<std::vector<HostAndPort>>();
};

/**
 * Returns the host we think is the current master or uasserts.
 *
 * This is a thin wrapper around getHostOrRefresh so this will also refresh our view if we
 * don't think there is a master at first. The main difference is that this will uassert
 * rather than returning an empty HostAndPort.
 */
HostAndPort TopologyManager::getMasterOrUassert() {
    // TODO
    return HostAndPort();
};

/**
 * Notifies this Monitor that a host has failed because of the specified error 'status' and
 * should be considered down.
 *
 * Call this when you get a connection error. If you get an error while trying to refresh our
 * view of a host, call Refresher::failedHost instead because it bypasses taking the monitor's
 * mutex.
 */
void TopologyManager::failedHost(const HostAndPort& host, const Status& status){
    // TODO
};

boost::optional<ServerDescriptionPtr> TopologyManager::_currentPrimary() const {
    const auto primaries = getTopologyDescription()->findServers(primaryPredicate);
    invariant(primaries.size() <= 1);  // TODO: check this invariant
    return (primaries.size()) ? boost::optional<ServerDescriptionPtr>(primaries[0]) : boost::none;
}

/**
 * Returns true if this node is the master based ONLY on local data. Be careful, return may
 * be stale.
 */
bool TopologyManager::isPrimary(const HostAndPort& host) const {
    const auto currentPrimary = _currentPrimary();
    if (currentPrimary) {
        return (*currentPrimary)->getAddress() == host.toString();
    }
    return false;
};

/**
 * Returns true if host is part of this set and is considered up (meaning it can accept
 * queries).
 */
bool TopologyManager::isHostUp(const HostAndPort& host) const {
    // TODO: this is probably the right impl since sdam removes servers that have problems
    return getTopologyDescription()->findServerByAddress(host.toString()) != boost::none;
};

/**
 * Returns the minimum wire version supported across the replica set.
 */
int TopologyManager::getMinWireVersion() const {
    const std::vector<ServerDescriptionPtr>& servers = getTopologyDescription()->getServers();
    if (servers.size() > 0) {
        const auto serverDescription =
            *std::min_element(servers.begin(), servers.end(), minWireCompare);
        return serverDescription->getMinWireVersion();
    } else {
        return -1;  // TODO: check this case / MONGO_UNREACHABLE?
    }
};

/**
 * Returns the maximum wire version supported across the replica set.
 */
int TopologyManager::getMaxWireVersion() const {
    const std::vector<ServerDescriptionPtr>& servers = getTopologyDescription()->getServers();
    if (servers.size() > 0) {
        const auto serverDescription =
            *std::max_element(servers.begin(), servers.end(), maxWireCompare);
        return serverDescription->getMaxWireVersion();
    } else {
        return -1;  // TODO: check this case / MONGO_UNREACHABLE?
    }
}

/**
 * The name of the set.
 */
std::string TopologyManager::getName() const {
    const auto setName = getTopologyDescription()->getSetName();
    return (setName) ? *setName : "";
};

/**
 * Returns a std::string with the format name/server1,server2.
 * If name is empty, returns just comma-separated list of servers.
 * It IS updated to reflect the current members of the set.
 */
std::string TopologyManager::getServerAddress() const {
    const auto topologyDescription = getTopologyDescription();
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
};

/**
 * Is server part of this set? Uses only cached information.
 */
bool TopologyManager::contains(const HostAndPort& server) const {
    return getTopologyDescription()->findServerByAddress(server.toString()) != boost::none;
};

/**
 * Writes information about our cached view of the set to a BSONObjBuilder. If
 * forFTDC, trim to minimize its size for full-time diagnostic data capture.
 */
void TopologyManager::appendInfo(BSONObjBuilder& b, bool forFTDC) const {
    MONGO_UNREACHABLE;
    // TODO
};

/**
 * Returns true if the monitor knows a usable primary from it's interal view.
 */
bool TopologyManager::isKnownToHaveGoodPrimary() const {
    return _currentPrimary() != boost::none;
};
};  // namespace mongo::sdam
