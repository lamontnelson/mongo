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
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork
#include "mongo/client/sdam/topology_state_machine.h"
#include "mongo/client/sdam/sdam_test_base.h"

#include <functional>

#include "mongo/util/log.h"

namespace mongo::sdam {
TopologyStateMachine::TopologyStateMachine(const SdamConfiguration& config) : _config(config) {
    initTransitionTable();
}

// This is used to make the syntax in initTransitionTable less verbose.
// Since we have enum class for TopologyType and ServerType there are no implicit int conversions.
template <typename T>
inline int idx(T enumType) {
    return static_cast<int>(enumType);
}

/**
 * This function encodes the transition table specified in
 * https://github.com/mongodb/specifications/blob/master/source/server-discovery-and-monitoring/server-discovery-and-monitoring.rst#topologytype-table
 */
void mongo::sdam::TopologyStateMachine::initTransitionTable() {
    using namespace std::placeholders;

    // init the table to No-ops
    const TransitionAction NO_OP([](const TopologyDescription&, const ServerDescription&) {});
    _stt.resize(allTopologyTypes().size() + 1);
    for (auto& row : _stt) {
        row.resize(allServerTypes().size() + 1, NO_OP);
    }

    // From TopologyType: Unknown
    _stt[idx(TopologyType::kUnknown)][idx(ServerType::kStandalone)] =
        std::bind(&TopologyStateMachine::updateUnknownWithStandalone, this, _1, _2);
    _stt[idx(TopologyType::kUnknown)][idx(ServerType::kMongos)] =
        setTopologyType(TopologyType::kSharded);
    _stt[idx(TopologyType::kUnknown)][idx(ServerType::kRSPrimary)] =
        setTopologyTypeAndUpdateRSFromPrimary(TopologyType::kReplicaSetWithPrimary);

    {
        const auto serverTypes = std::vector<ServerType>{
            ServerType::kRSSecondary, ServerType::kRSArbiter, ServerType::kRSOther};
        for (auto newServerType : serverTypes) {
            _stt[idx(TopologyType::kUnknown)][idx(newServerType)] =
                setTopologyTypeAndUpdateRSWithoutPrimary(TopologyType::kReplicaSetNoPrimary);
        }
    }

    // From TopologyType: Sharded
    {
        const auto serverTypes = std::vector<ServerType>{ServerType::kStandalone,
                                                         ServerType::kRSPrimary,
                                                         ServerType::kRSSecondary,
                                                         ServerType::kRSArbiter,
                                                         ServerType::kRSOther,
                                                         ServerType::kRSGhost};
        for (auto newServerType : serverTypes) {
            _stt[idx(TopologyType::kSharded)][idx(newServerType)] =
                std::bind(&TopologyStateMachine::removeAndStopMonitoring, this, _1, _2);
        }
    }

    // From TopologyType: ReplicaSetNoPrimary
    {
        const auto serverTypes =
            std::vector<ServerType>{ServerType::kStandalone, ServerType::kMongos};
        for (auto serverType : serverTypes) {
            _stt[idx(TopologyType::kReplicaSetNoPrimary)][idx(serverType)] =
                std::bind(&TopologyStateMachine::removeAndStopMonitoring, this, _1, _2);
        }
    }

    _stt[idx(TopologyType::kReplicaSetNoPrimary)][idx(ServerType::kRSPrimary)] =
        setTopologyTypeAndUpdateRSFromPrimary(TopologyType::kReplicaSetWithPrimary);

    {
        const auto serverTypes = std::vector<ServerType>{
            ServerType::kRSSecondary, ServerType::kRSArbiter, ServerType::kRSOther};
        for (auto serverType : serverTypes) {
            _stt[idx(TopologyType::kReplicaSetNoPrimary)][idx(serverType)] =
                std::bind(&TopologyStateMachine::updateRSWithoutPrimary, this, _1, _2);
        }
    }

    // From TopologyType: ReplicaSetWithPrimary
    {
        const auto serverTypes =
            std::vector<ServerType>{ServerType::kUnknown, ServerType::kRSGhost};
        for (auto serverType : serverTypes) {
            _stt[idx(TopologyType::kReplicaSetWithPrimary)][idx(serverType)] =
                std::bind(&TopologyStateMachine::checkIfHasPrimary, this, _1, _2);
        }
    }

    {
        const auto serverTypes =
            std::vector<ServerType>{ServerType::kStandalone, ServerType::kMongos};
        for (auto serverType : serverTypes) {
            _stt[idx(TopologyType::kReplicaSetWithPrimary)][idx(serverType)] =
                std::bind(&TopologyStateMachine::removeAndCheckIfHasPrimary, this, _1, _2);
        }
    }

    _stt[idx(TopologyType::kReplicaSetWithPrimary)][idx(ServerType::kRSPrimary)] =
        std::bind(&TopologyStateMachine::updateRSFromPrimary, this, _1, _2);

    {
        const auto serverTypes = std::vector<ServerType>{
            ServerType::kRSSecondary, ServerType::kRSArbiter, ServerType::kRSOther};
        for (auto serverType : serverTypes) {
            _stt[idx(TopologyType::kReplicaSetWithPrimary)][idx(serverType)] =
                std::bind(&TopologyStateMachine::updateRSWithPrimaryFromMember, this, _1, _2);
        }
    }
}

void TopologyStateMachine::nextServerDescription(const TopologyDescription& topologyDescription,
                                                 const ServerDescription& serverDescription) {
    stdx::lock_guard<mongo::Mutex> lock(_mutex);

    bool isExistingServer =
        topologyDescription.containsServerAddress(serverDescription.getAddress());

    if (isExistingServer) {
        emitReplaceServer(serverDescription);
    } else {
        emitNewServer(serverDescription);
    }

    if (topologyDescription.getType() != TopologyType::kSingle) {
        auto& action = _stt[idx(topologyDescription.getType())][idx(serverDescription.getType())];
        action(topologyDescription, serverDescription);
    }
}

void TopologyStateMachine::updateUnknownWithStandalone(
    const TopologyDescription& topologyDescription, const ServerDescription& serverDescription) {
    if (!topologyDescription.containsServerAddress(serverDescription.getAddress()))
        return;

    if (_config.getSeedList() && (*_config.getSeedList()).size() == 1) {
        emitTypeChange(TopologyType::kSingle);
    } else {
        emitServerRemoved(serverDescription);
    }
}

void TopologyStateMachine::updateRSWithoutPrimary(const TopologyDescription& topologyDescription,
                                                  const ServerDescription& serverDescription) {
    const auto& serverDescAddress = serverDescription.getAddress();

    if (!topologyDescription.containsServerAddress(serverDescAddress))
        return;

    const auto& currentSetName = topologyDescription.getSetName();
    const auto& serverDescSetName = serverDescription.getSetName();
    if (currentSetName == boost::none) {
        emitNewSetName(serverDescSetName);
    } else if (currentSetName != serverDescSetName) {
        emitServerRemoved(serverDescription);
        return;
    }

    addUnknownServers(topologyDescription, serverDescription);

    if (serverDescription.getPrimary()) {
        const auto& primaryAddress = *serverDescription.getPrimary();
        auto primaries = topologyDescription.findServers(
            [primaryAddress](const ServerDescription& description) -> bool {
                return description.getAddress() == primaryAddress;
            });
        invariant(primaries.size() <= 1);
        if (primaries.size() == 1) {
            const auto& server = primaries.back();
            if (server.getType() == ServerType::kUnknown) {
                emitUpdateServerType(server, ServerType::kPossiblePrimary);
            }
        }
    }

    if (serverDescAddress != serverDescription.getMe()) {
        emitServerRemoved(serverDescription);
    }
}
void TopologyStateMachine::addUnknownServers(const TopologyDescription& topologyDescription,
                                             const ServerDescription& serverDescription) {
    const std::set<ServerAddress>* addressSets[3]{&serverDescription.getHosts(),
                                                  &serverDescription.getPassives(),
                                                  &serverDescription.getArbiters()};
    for (const auto addresses : addressSets) {
        for (const auto addressFromSet : *addresses) {
            if (!topologyDescription.containsServerAddress(addressFromSet)) {
                emitNewServer(ServerDescription(addressFromSet));
            }
        }
    }
}

void TopologyStateMachine::updateRSWithPrimaryFromMember(
    const TopologyDescription& topologyDescription, const ServerDescription& serverDescription) {
    const auto& serverDescAddress = serverDescription.getAddress();
    if (!topologyDescription.containsServerAddress(serverDescAddress)) {
        return;
    }

    invariant(serverDescription.getSetName() != boost::none);
    if (topologyDescription.getSetName() != serverDescription.getSetName()) {
        removeAndCheckIfHasPrimary(topologyDescription, serverDescription);
        return;
    }

    if (serverDescription.getAddress() != serverDescription.getMe()) {
        removeAndCheckIfHasPrimary(topologyDescription, serverDescription);
        return;
    }

    auto primaries = topologyDescription.findServers([](const ServerDescription& description) {
        return description.getType() == ServerType::kRSPrimary;
    });
    if (primaries.size() == 0) {
        emitTypeChange(TopologyType::kReplicaSetNoPrimary);
        if (serverDescription.getPrimary() != boost::none) {
            auto possiblePrimary = topologyDescription.findServers(
                [serverDescription](const ServerDescription& description) {
                    return description.getAddress() == serverDescription.getPrimary();
                });
            invariant(possiblePrimary.size() == 1);
            if (possiblePrimary[0].getType() == ServerType::kUnknown) {
                emitUpdateServerType(possiblePrimary.front(), ServerType::kPossiblePrimary);
            }
        }
    }
}

void TopologyStateMachine::updateRSFromPrimary(const TopologyDescription& topologyDescription,
                                               const ServerDescription& serverDescription) {
    const auto& serverDescAddress = serverDescription.getAddress();
    if (!topologyDescription.containsServerAddress(serverDescAddress)) {
        return;
    }

    auto topologySetName = topologyDescription.getSetName();
    auto serverDescSetName = serverDescription.getSetName();
    if (!topologySetName && serverDescSetName) {
        emitNewSetName(serverDescSetName);
    } else if (topologySetName != serverDescSetName) {
        // We found a primary but it doesn't have the setName
        // provided by the user or previously discovered.
        removeAndCheckIfHasPrimary(topologyDescription, serverDescription);
        return;
    }

    auto serverDescSetVersion = serverDescription.getSetVersion();
    auto serverDescElectionId = serverDescription.getElectionId();
    auto topologyMaxSetVersion = topologyDescription.getMaxSetVersion();
    auto topologyMaxElectionId = topologyDescription.getMaxElectionId();
    if (serverDescSetVersion && serverDescElectionId) {
        if (topologyMaxSetVersion && topologyMaxElectionId &&
            ((topologyMaxSetVersion > serverDescSetVersion) ||
             (topologyMaxSetVersion == serverDescSetVersion &&
              (*topologyMaxElectionId).compare(*serverDescElectionId) > 0))) {
            // stale primary
            emitReplaceServer(ServerDescription(serverDescAddress));
            checkIfHasPrimary(topologyDescription, serverDescription);
            return;
        }
        emitNewMaxElectionId(*serverDescription.getElectionId());
    }

    if (serverDescSetVersion &&
        (!topologyMaxSetVersion || (serverDescSetVersion > topologyMaxSetVersion))) {
        emitNewMaxSetVersion(*serverDescSetVersion);
    }

    auto oldPrimaries =
        topologyDescription.findServers([serverDescAddress](const ServerDescription& description) {
            return (description.getAddress() != serverDescAddress &&
                    description.getType() == ServerType::kRSPrimary);
        });
    invariant(oldPrimaries.size() <= 1);  // TODO: verify this
    for (const auto& server : oldPrimaries) {
        emitReplaceServer(ServerDescription(server.getAddress()));
    }

    addUnknownServers(topologyDescription, serverDescription);

    for (const auto& currentServerDescription : topologyDescription.getServers()) {
        const auto currentServerAddress = currentServerDescription.getAddress();
        auto hosts = serverDescription.getHosts().find(currentServerAddress);
        auto passives = serverDescription.getPassives().find(currentServerAddress);
        auto arbiters = serverDescription.getArbiters().find(currentServerAddress);

        if (hosts == serverDescription.getHosts().end() &&
            passives == serverDescription.getPassives().end() &&
            arbiters == serverDescription.getArbiters().end()) {
            emitServerRemoved(currentServerDescription);
        }
    }

    checkIfHasPrimary(topologyDescription, serverDescription);
}

void TopologyStateMachine::removeAndStopMonitoring(const TopologyDescription&,
                                                   const ServerDescription& serverDescription) {
    emitServerRemoved(serverDescription);
}

void TopologyStateMachine::checkIfHasPrimary(const TopologyDescription& topologyDescription,
                                             const ServerDescription& serverDescription) {
    auto foundPrimaries = topologyDescription.findServers([](const ServerDescription& description) {
        return description.getType() == ServerType::kRSPrimary;
    });
    if (foundPrimaries.size() > 0) {
        emitTypeChange(TopologyType::kReplicaSetWithPrimary);
    } else {
        emitTypeChange(TopologyType::kReplicaSetNoPrimary);
    }
}

void TopologyStateMachine::removeAndCheckIfHasPrimary(
    const TopologyDescription& topologyDescription, const ServerDescription& serverDescription) {
    removeAndStopMonitoring(topologyDescription, serverDescription);
    checkIfHasPrimary(topologyDescription, serverDescription);
}

TransitionAction TopologyStateMachine::setTopologyType(TopologyType type) {
    return [this, type](const TopologyDescription& topologyDescription,
                        const ServerDescription& newServerDescription) { emitTypeChange(type); };
}

TransitionAction TopologyStateMachine::setTopologyTypeAndUpdateRSFromPrimary(TopologyType type) {
    return [this, type](const TopologyDescription& topologyDescription,
                        const ServerDescription& newServerDescription) {
        std::cout << "change type to " << toString(type) << std::endl;
        emitTypeChange(type);
        updateRSFromPrimary(topologyDescription, newServerDescription);
    };
}

TransitionAction TopologyStateMachine::setTopologyTypeAndUpdateRSWithoutPrimary(TopologyType type) {
    return [this, type](const TopologyDescription& topologyDescription,
                        const ServerDescription& newServerDescription) {
        emitTypeChange(type);
        updateRSWithoutPrimary(topologyDescription, newServerDescription);
    };
}

void TopologyStateMachine::emit(std::shared_ptr<TopologyStateMachineEvent> event) {
    for (auto& observer : _observers) {
        observer->onTopologyStateMachineEvent(event);
    }
}

void TopologyStateMachine::emitServerRemoved(const ServerDescription& serverDescription) {
    std::shared_ptr<TopologyStateMachineEvent> event =
        std::make_shared<RemoveServerDescriptionEvent>(serverDescription);
    emit(event);
}

void TopologyStateMachine::emitTypeChange(TopologyType topologyType) {
    std::shared_ptr<TopologyStateMachineEvent> event =
        std::make_shared<TopologyTypeChangeEvent>(topologyType);
    emit(event);
}

void TopologyStateMachine::emitNewSetName(const boost::optional<std::string>& setName) {
    std::shared_ptr<TopologyStateMachineEvent> event = std::make_shared<NewSetNameEvent>(setName);
    emit(event);
}

void TopologyStateMachine::emitNewServer(ServerDescription newServerDescription) {
    std::shared_ptr<TopologyStateMachineEvent> event =
        std::make_shared<NewServerDescriptionEvent>(newServerDescription);
    emit(event);
}

void TopologyStateMachine::emitUpdateServerType(const ServerDescription& serverDescription,
                                                ServerType newServerType) {
    std::shared_ptr<TopologyStateMachineEvent> event =
        std::make_shared<UpdateServerTypeEvent>(serverDescription, newServerType);
    emit(event);
}

void TopologyStateMachine::emitReplaceServer(ServerDescription updatedServerDescription) {
    std::shared_ptr<TopologyStateMachineEvent> event =
        std::make_shared<UpdateServerDescriptionEvent>(updatedServerDescription);
    emit(event);
}

void TopologyStateMachine::emitNewMaxElectionId(const OID& newMaxElectionId) {
    std::shared_ptr<TopologyStateMachineEvent> event =
        std::make_shared<NewMaxElectionIdEvent>(newMaxElectionId);
    emit(event);
}

void TopologyStateMachine::emitNewMaxSetVersion(int& newMaxSetVersion) {
    std::shared_ptr<TopologyStateMachineEvent> event =
        std::make_shared<NewMaxSetVersionEvent>(newMaxSetVersion);
    emit(event);
}

void TopologyStateMachine::addObserver(std::shared_ptr<TopologyObserver> observer) {
    stdx::lock_guard<mongo::Mutex> lock(_mutex);
    _observers.push_back(std::move(observer));
}
}  // namespace mongo::sdam
