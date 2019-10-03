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
#include "mongo/client/sdam/topology_description.h"
#include "mongo/client/sdam/sdam_test_base.h"

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork
#include "mongo/platform/basic.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"

namespace mongo::sdam {
TopologyDescription::TopologyDescription(SdamConfiguration config)
    : _type(config.getInitialType()), _setName(config.getSetName()), _topologyObserver(*this) {

    if (auto seeds = config.getSeedList()) {
        _servers.clear();
        for (auto address : *seeds) {
            this->installServerDescription(ServerDescription(address));
        }
    }
}

void TopologyDescription::onNewServerDescription(ServerDescription newDescription) {
    // TODO:
}

bool TopologyDescription::hasReadableServer(boost::optional<ReadPreference> readPreference) {
    return false;
}

bool TopologyDescription::hasWritableServer() {
    return false;
}

const UUID& TopologyDescription::getId() const {
    return _id;
}

TopologyType TopologyDescription::getType() const {
    return _type;
}

const boost::optional<std::string>& TopologyDescription::getSetName() const {
    return _setName;
}

const boost::optional<int>& TopologyDescription::getMaxSetVersion() const {
    return _maxSetVersion;
}

const boost::optional<OID>& TopologyDescription::getMaxElectionId() const {
    return _maxElectionId;
}

const std::vector<ServerDescription>& TopologyDescription::getServers() const {
    return _servers;
}

bool TopologyDescription::isWireVersionCompatible() const {
    return _compatible;
}

const boost::optional<std::string>& TopologyDescription::getWireVersionCompatibleError() const {
    return _compatibleError;
}

const boost::optional<int>& TopologyDescription::getLogicalSessionTimeoutMinutes() const {
    return _logicalSessionTimeoutMinutes;
}

void TopologyDescription::setType(TopologyType type) {
    _type = type;
}

bool TopologyDescription::containsServerAddress(ServerAddress address) const {
    // TODO: index by address
    boost::to_lower(address);
    for (auto& serverDescription : _servers) {
        if (serverDescription.getAddress() == address)
            return true;
    }
    return false;
}

std::vector<ServerDescription> TopologyDescription::findServers(
    std::function<bool(const ServerDescription&)> predicate) {
    std::vector<ServerDescription> result;
    for (const auto& server : _servers) {
        if (predicate(server)) {
            result.push_back(server);
        }
    }
    return result;
}
void TopologyDescription::installServerDescription(const ServerDescription& newServerDescription) {
    for (auto it = _servers.begin(); it != _servers.end(); ++it) {
        const auto& currentDescription = *it;
        if (currentDescription.getAddress() == newServerDescription.getAddress()) {
            *it = newServerDescription;
            return;
        }
    }
    _servers.push_back(newServerDescription);
}


SdamConfiguration::SdamConfiguration(boost::optional<std::vector<ServerAddress>> seedList,
                                     TopologyType initialType,
                                     mongo::Milliseconds heartBeatFrequencyMs,
                                     boost::optional<std::string> setName)
    : _seedList(seedList),
      _initialType(initialType),
      _heartBeatFrequencyMs(heartBeatFrequencyMs),
      _setName(setName) {
    if (_initialType == TopologyType::kSingle) {
        uassert(ErrorCodes::InvalidSeedList,
                "A single TopologyType must have exactly one entry in the seed list.",
                (*seedList).size() == 1);
    }

    if (_setName) {
        uassert(ErrorCodes::InvalidTopologyType,
                "Only ReplicaSetNoPrimary allowed when a setName is provided.",
                _initialType == TopologyType::kReplicaSetNoPrimary);
    }

    if (seedList) {
        uassert(
            ErrorCodes::InvalidSeedList, "seed list size must be >= 1", (*seedList).size() >= 1);
    }

    uassert(ErrorCodes::InvalidHeartBeatFrequency,
            "topology heartbeat must be >= 500ms",
            _heartBeatFrequencyMs >= mongo::Milliseconds(500));
}

const boost::optional<std::vector<ServerAddress>>& SdamConfiguration::getSeedList() const {
    return _seedList;
}

TopologyType SdamConfiguration::getInitialType() const {
    return _initialType;
}

Milliseconds SdamConfiguration::getHeartBeatFrequency() const {
    return _heartBeatFrequencyMs;
}

Milliseconds SdamConfiguration::getMinHeartbeatFrequencyMs() const {
    return _minHeartbeatFrequencyMS;
}
const boost::optional<std::string>& SdamConfiguration::getSetName() const {
    return _setName;
}

void TopologyDescription::Observer::onTypeChange(TopologyType topologyType) {
    _parent.setType(topologyType);
}
void TopologyDescription::Observer::onNewSetName(boost::optional<std::string> setName) {
    _parent._setName = setName;
}
void TopologyDescription::Observer::onUpdatedServerType(const ServerDescription& serverDescription,
                                                        ServerType newServerType) {
    // TODO: need to make ServerDescriptionBuilder start from an existing instance.
}
void TopologyDescription::Observer::onNewMaxElectionId(const OID& newMaxElectionId) {
    _parent._maxElectionId = newMaxElectionId;
}
void TopologyDescription::Observer::onNewMaxSetVersion(int newMaxSetVersion) {
    _parent._maxSetVersion = newMaxSetVersion;
}
void TopologyDescription::Observer::onNewServerDescription(
    const ServerDescription& newServerDescription) {
    _parent.installServerDescription(newServerDescription);
    std::cout << "after install: " << _parent.getServers() << std::endl;
}
void TopologyDescription::Observer::onUpdateServerDescription(
    const ServerDescription& serverDescription) {
    std::cout << "here!!!!!!!!!! " << serverDescription;
    _parent.installServerDescription(serverDescription);
}
void TopologyDescription::Observer::onServerDescriptionRemoved(
    const ServerDescription& serverDescription) {
    auto& servers = _parent._servers;
    auto it = std::find_if(
        servers.begin(), servers.end(), [serverDescription](const ServerDescription& description) {
            return serverDescription.getAddress() == description.getAddress();
        });
    if (it != servers.end()) {
        servers.erase(it);
    }
}
};  // namespace mongo::sdam
