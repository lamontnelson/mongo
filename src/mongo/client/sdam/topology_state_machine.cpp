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
    _stt.resize(allTopologyTypes().size() + 1);
    for (auto& row : _stt) {
        row.resize(allServerTypes().size() + 1);
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

void TopologyStateMachine::nextServerDescription(TopologyDescription& topologyDescription,
                                                 const ServerDescription& serverDescription) {
    stdx::lock_guard<mongo::Mutex> lock(_mutex);
    topologyDescription.installServerDescription(serverDescription);
    auto& action = _stt[idx(topologyDescription.getType())][idx(serverDescription.getType())];
    action(topologyDescription, serverDescription);
}

void TopologyStateMachine::updateUnknownWithStandalone(TopologyDescription& topologyDescription,
                                                       const ServerDescription& serverDescription) {
    if (!topologyDescription.containsServerAddress(serverDescription.getAddress()))
        return;

    if (_config.getSeedList() && (*_config.getSeedList()).size() == 1) {
        emitTypeChange(TopologyType::kSingle);
    } else {
        emitServerRemoved(serverDescription);
    }
}

void TopologyStateMachine::updateRSWithoutPrimary(TopologyDescription& topologyDescription,
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
    TopologyDescription& topologyDescription, const ServerDescription& serverDescription) {
    // if description.address not in topologyDescription.servers:
    //  # While we were checking this server, another thread heard from the
    //  # primary that this server is not in the replica set.
    //  return
    //
    const auto& serverDescAddress = serverDescription.getAddress();
    if (!topologyDescription.containsServerAddress(serverDescAddress)) {
        return;
    }

    //# SetName is never null here.
    // if topologyDescription.setName != description.setName:
    //    remove this server from topologyDescription and stop monitoring it
    //    checkIfHasPrimary()
    //    return
    //
    invariant(serverDescription.getSetName() != boost::none);
    if (topologyDescription.getSetName() != serverDescription.getSetName()) {
        removeAndCheckIfHasPrimary(topologyDescription, serverDescription);
        return;
    }


    // if description.address != description.me:
    //    remove this server from topologyDescription and stop monitoring it
    //    checkIfHasPrimary()
    //    return
    if (serverDescription.getAddress() != serverDescription.getMe()) {
        removeAndCheckIfHasPrimary(topologyDescription, serverDescription);
        return;
    }

    //# Had this member been the primary?
    // if there is no primary in topologyDescription.servers:
    //    topologyDescription.type = ReplicaSetNoPrimary
    //
    //    if description.primary is not null:
    //        find the ServerDescription in topologyDescription.servers whose
    //        address equals description.primary
    //
    //        if its type is Unknown, change its type to PossiblePrimary
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
            invariant(possiblePrimary.size() == 1);  // TODO: verify this
            if (possiblePrimary[0].getType() == ServerType::kUnknown) {
                // TODO: complete when we move the ServerDescription mutation code
                throw "not implemented yet";
            }
        }
    }
}

void TopologyStateMachine::updateRSFromPrimary(TopologyDescription& topologyDescription,
                                               const ServerDescription& serverDescription) {
    // if description.address not in topologyDescription.servers:
    //  return
    const auto& serverDescAddress = serverDescription.getAddress();
    if (!topologyDescription.containsServerAddress(serverDescAddress)) {
        return;
    }

    // if topologyDescription.setName is null:
    //    topologyDescription.setName = description.setName
    auto topologySetName = topologyDescription.getSetName();
    auto serverDescSetName = serverDescription.getSetName();
    if (topologySetName == boost::none) {
        emitNewSetName(serverDescSetName);
    }

    // else if topologyDescription.setName != description.setName:
    //    # We found a primary but it doesn't have the setName
    //    # provided by the user or previously discovered.
    //    remove this server from topologyDescription and stop monitoring it
    //    checkIfHasPrimary()
    //    return
    else if (topologySetName != serverDescSetName) {
        // We found a primary but it doesn't have the setName
        // provided by the user or previously discovered.
        removeAndCheckIfHasPrimary(topologyDescription, serverDescription);
        return;
    }
    //
    // if description.setVersion is not null and description.electionId is not null:
    //    # Election ids are ObjectIds, see
    //    # "using setVersion and electionId to detect stale primaries"
    //    # for comparison rules.
    //    if (topologyDescription.maxSetVersion is not null and
    //        topologyDescription.maxElectionId is not null and (
    //            topologyDescription.maxSetVersion > description.setVersion or (
    //                topologyDescription.maxSetVersion == description.setVersion and
    //                topologyDescription.maxElectionId > description.electionId
    //            )
    //        ):
    //
    //        # Stale primary.
    //        replace description with a default ServerDescription of type "Unknown"
    //        checkIfHasPrimary()
    //        return
    //
    //    topologyDescription.maxElectionId = description.electionId
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


    //
    // if (description.setVersion is not null and
    //    (topologyDescription.maxSetVersion is null or
    //        description.setVersion > topologyDescription.maxSetVersion)):
    //
    //    topologyDescription.maxSetVersion = description.setVersion
    if (serverDescSetVersion &&
        (!topologyMaxSetVersion || (serverDescSetVersion > topologyMaxSetVersion))) {
        emitNewMaxSetVersion(*serverDescSetVersion);
    }
    //
    // for each server in topologyDescription.servers:
    //    if server.address != description.address:
    //        if server.type is RSPrimary:
    //            # See note below about invalidating an old primary.
    //            replace the server with a default ServerDescription of type "Unknown"
    auto oldPrimaries =
        topologyDescription.findServers([serverDescAddress](const ServerDescription& description) {
            return (description.getAddress() != serverDescAddress &&
                    description.getType() == ServerType::kRSPrimary);
        });
    invariant(oldPrimaries.size() <= 1);  // TODO: verify this
    for (const auto& server : oldPrimaries) {
        emitReplaceServer(ServerDescription(server.getAddress()));
    }

    // for each address in description's "hosts", "passives", and "arbiters":
    //    if address is not in topologyDescription.servers:
    //        add new default ServerDescription of type "Unknown"
    //        begin monitoring the new server
    addUnknownServers(topologyDescription, serverDescription);

    // for each server in topologyDescription.servers:
    //    if server.address not in description's "hosts", "passives", or "arbiters":
    //        remove the server and stop monitoring it
    for (const auto& currentServerDescription : topologyDescription.getServers()) {
        const auto serverAddress = currentServerDescription.getAddress();
        auto hosts = currentServerDescription.getHosts().find(serverAddress);
        auto passives = currentServerDescription.getPassives().find(serverAddress);
        auto arbiters = currentServerDescription.getArbiters().find(serverAddress);

        if (hosts == currentServerDescription.getHosts().end() &&
            passives == currentServerDescription.getPassives().end() &&
            arbiters == currentServerDescription.getArbiters().end()) {
            emitServerRemoved(currentServerDescription);
        }
    }

    checkIfHasPrimary(topologyDescription, serverDescription);
}

void TopologyStateMachine::removeAndStopMonitoring(TopologyDescription&,
                                                   const ServerDescription& serverDescription) {
    emitServerRemoved(serverDescription);
}

void TopologyStateMachine::checkIfHasPrimary(TopologyDescription& topologyDescription,
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

void TopologyStateMachine::removeAndCheckIfHasPrimary(TopologyDescription& topologyDescription,
                                                      const ServerDescription& serverDescription) {
    removeAndStopMonitoring(topologyDescription, serverDescription);
    checkIfHasPrimary(topologyDescription, serverDescription);
}

TransitionAction TopologyStateMachine::setTopologyType(TopologyType type) {
    return [this, type](TopologyDescription& topologyDescription,
                        const ServerDescription& newServerDescription) { emitTypeChange(type); };
}

TransitionAction TopologyStateMachine::setTopologyTypeAndUpdateRSFromPrimary(TopologyType type) {
    return [this, type](TopologyDescription& topologyDescription,
                        const ServerDescription& newServerDescription) {
        emitTypeChange(type);
        updateRSFromPrimary(topologyDescription, newServerDescription);
    };
}

TransitionAction TopologyStateMachine::setTopologyTypeAndUpdateRSWithoutPrimary(TopologyType type) {
    return [this, type](TopologyDescription& topologyDescription,
                        const ServerDescription& newServerDescription) {
        emitTypeChange(type);
        updateRSWithoutPrimary(topologyDescription, newServerDescription);
    };
}

// TODO: refactor these for redundancy
void TopologyStateMachine::emitServerRemoved(const ServerDescription& serverDescription) {
    for (auto& observer : _observers) {
        observer->onServerDescriptionRemoved(serverDescription);
    }
}

void TopologyStateMachine::emitTypeChange(TopologyType topologyType) {
    for (auto& observer : _observers) {
        observer->onTypeChange(topologyType);
    }
}

void TopologyStateMachine::emitNewSetName(const boost::optional<std::string>& setName) {
    for (auto& observer : _observers) {
        observer->onNewSetName(setName);
    }
}

void TopologyStateMachine::emitNewServer(ServerDescription newServerDescription) {
    for (auto& observer : _observers) {
        observer->onNewServerDescription(newServerDescription);
    }
}

void TopologyStateMachine::emitUpdateServerType(const ServerDescription& serverDescription,
                                                ServerType newServerType) {
    for (auto& observer : _observers) {
        observer->onUpdatedServerType(serverDescription, newServerType);
    }
}

void TopologyStateMachine::emitReplaceServer(ServerDescription newServerDescription) {
    for (auto& observer : _observers) {
        observer->onUpdateServerDescription(newServerDescription);
    }
}

void TopologyStateMachine::emitNewMaxElectionId(const OID& newMaxElectionId) {
    for (auto& observer : _observers) {
        observer->onNewMaxElectionId(newMaxElectionId);
    }
}

void TopologyStateMachine::emitNewMaxSetVersion(int& newMaxSetVersion) {
    for (auto& observer : _observers) {
        observer->onNewMaxSetVersion(newMaxSetVersion);
    }
}

void mongo::sdam::TopologyStateMachine::addObserver(std::shared_ptr<TopologyObserver> observer) {
    stdx::lock_guard<mongo::Mutex> lock(_mutex);
    _observers.push_back(std::move(observer));
}
}  // namespace mongo::sdam
