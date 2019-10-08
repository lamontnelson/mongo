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
#include "mongo/client/sdam/server_description.h"
#include "mongo/db/wire_version.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"

namespace mongo::sdam {
////////////////////////
// TopologyDescription
////////////////////////
TopologyDescription::TopologyDescription(SdamConfiguration config)
    : _type(config.getInitialType()),
      _setName(config.getSetName()),
      _topologyObserver(std::make_shared<Observer>(*this)) {
    if (auto seeds = config.getSeedList()) {
        _servers.clear();
        for (auto address : *seeds) {
            _servers.push_back(ServerDescription(address));
        }
    }
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
    std::function<bool(const ServerDescription&)> predicate) const {
    std::vector<ServerDescription> result;
    for (const auto& server : _servers) {
        if (predicate(server)) {
            result.push_back(server);
        }
    }
    return result;
}

boost::optional<ServerDescription> TopologyDescription::installServerDescription(
    const ServerDescription& newServerDescription) {
    boost::optional<ServerDescription> previousDescription;
    if (getType() == TopologyType::kSingle) {
        // For Single, there is always one ServerDescription in TopologyDescription.servers;
        // the ServerDescription in TopologyDescription.servers MUST be replaced with the new
        // ServerDescription.
        invariant(_servers.size() == 1);
        previousDescription = _servers[0];
        _servers[0] = newServerDescription;
    } else {
        for (auto it = _servers.begin(); it != _servers.end(); ++it) {
            const auto& currentDescription = *it;
            if (currentDescription.getAddress() == newServerDescription.getAddress()) {
                previousDescription = *it;
                *it = newServerDescription;
                break;
            }
        }
        _servers.push_back(newServerDescription);
    }
    return previousDescription;
}

void TopologyDescription::removeServerDescription(const ServerDescription& serverDescription) {
    auto it = std::find_if(_servers.begin(),
                           _servers.end(),
                           [serverDescription](const ServerDescription& description) {
                               return serverDescription.getAddress() == description.getAddress();
                           });
    if (it != _servers.end()) {
        _servers.erase(it);
    }
}

void TopologyDescription::checkWireCompatibilityVersions() {
    const WireVersionInfo supportedWireVersion = WireSpec::instance().outgoing;
    std::ostringstream errorOss;

    _compatible = true;
    for (const auto& serverDescription : _servers) {
        if (serverDescription.getType() == ServerType::kUnknown) {
            continue;
        }

        if (serverDescription.getMinWireVersion() > supportedWireVersion.maxWireVersion) {
            _compatible = false;
            errorOss << "Server at " << serverDescription.getAddress() << " requires wire version "
                     << serverDescription.getMinWireVersion()
                     << " but this version of mongo only supports up to "
                     << supportedWireVersion.maxWireVersion << ".";
            break;
        } else if (serverDescription.getMaxWireVersion() < supportedWireVersion.minWireVersion) {
            _compatible = false;
            const auto& mongoVersion =
                minimumRequiredMongoVersionString(supportedWireVersion.minWireVersion);
            errorOss << "Server at " << serverDescription.getAddress() << " requires wire version "
                     << serverDescription.getMaxWireVersion()
                     << " but this version of mongo requires at least "
                     << supportedWireVersion.minWireVersion << " (MongoDB " << mongoVersion << ").";
            break;
        }
    }

    _compatibleError = (_compatible) ? boost::none : boost::make_optional(errorOss.str());
}

const std::string TopologyDescription::minimumRequiredMongoVersionString(int version) {
    // TODO: need versions for AGG_RETURNS_CURSORS, BATCH_COMMANDS,
    // FIND_COMMAND, COMMANDS_ACCEPT_WRITE_CONCERN
    switch (version) {
        case RELEASE_2_4_AND_BEFORE:
            return "1.0";
        case RELEASE_2_7_7:
            return "2.7.7";
        case SUPPORTS_OP_MSG:
            return "3.6";
        case REPLICA_SET_TRANSACTIONS:
            return "4.0";
        case SHARDED_TRANSACTIONS:
            return "4.2";
        case PLACEHOLDER_FOR_44:
            return "4.4";
        default:
            return "UNKNOWN";
    }
}

const std::shared_ptr<TopologyObserver> TopologyDescription::getTopologyObserver() const {
    return _topologyObserver;
}

void TopologyDescription::Observer::onTopologyStateMachineEvent(
    std::shared_ptr<TopologyStateMachineEvent> e) {
    switch (e->type) {
        case TopologyStateMachineEventType::kNewMaxElectionId: {
            OID& newMaxElectionId =
                checked_pointer_cast<NewMaxElectionIdEvent>(e)->newMaxElectionId;
            _parent._maxElectionId = newMaxElectionId;
            LOG(3) << "SDAM: Topology max election id changed to: " << newMaxElectionId
                   << std::endl;
            break;
        }
        case TopologyStateMachineEventType::kNewMaxSetVersion: {
            int newMaxSetVersion = checked_pointer_cast<NewMaxSetVersionEvent>(e)->newMaxSetVersion;
            LOG(3) << "SDAM: Topology set name changed to: " << newMaxSetVersion << std::endl;
            _parent._maxSetVersion = newMaxSetVersion;
        } break;
        case TopologyStateMachineEventType::kNewSetName: {
            const boost::optional<std::string>& newSetName =
                checked_pointer_cast<NewSetNameEvent>(e)->newSetName;
            LOG(3) << "SDAM: Topology set name changed to: " << newSetName << std::endl;
            _parent._setName = newSetName;
            break;
        }
        case TopologyStateMachineEventType::kTopologyTypeChange: {
            const TopologyType newType = checked_pointer_cast<TopologyTypeChangeEvent>(e)->newType;
            LOG(3) << "SDAM: Topology type changed to: " << toString(newType) << std::endl;
            _parent.setType(newType);
            break;
        }
        case TopologyStateMachineEventType::kUpdateServerType:
            // TODO: need to make ServerDescriptionBuilder start from an existing instance.
            break;
        case TopologyStateMachineEventType::kNewServerDescription: {
            const auto& serverDescription =
                checked_pointer_cast<NewServerDescriptionEvent>(e)->newServerDescription;
            LOG(3) << "SDAM: Install new server description: " << serverDescription << std::endl;
            _parent.installServerDescription(serverDescription);
            _parent.checkWireCompatibilityVersions();
            break;
        }
        case TopologyStateMachineEventType::kUpdateServerDescription: {
            const auto& serverDescription =
                checked_pointer_cast<UpdateServerDescriptionEvent>(e)->updatedServerDescription;
            LOG(3) << "SDAM: Replace existing server description: " << serverDescription
                   << std::endl;
            _parent.installServerDescription(serverDescription);
            _parent.checkWireCompatibilityVersions();
            break;
        }
        case TopologyStateMachineEventType::kRemoveServerDescription: {
            const auto& serverDescription =
                checked_pointer_cast<RemoveServerDescriptionEvent>(e)->removedServerDescription;
            _parent.removeServerDescription(serverDescription);
            break;
        }
    }
}


////////////////////////
// SdamConfiguration
////////////////////////
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
};  // namespace mongo::sdam
