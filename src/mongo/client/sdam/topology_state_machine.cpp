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
#include "mongo/client/sdam/topology_state_machine.h"

#include <functional>

namespace mongo::sdam {
using namespace std::placeholders;

TopologyStateMachine::TopologyStateMachine(const SdamConfiguration& config) {
    initTransitionTable();
}

/**
 * This function encodes the transition table specified in
 * https://github.com/mongodb/specifications/blob/master/source/server-discovery-and-monitoring/server-discovery-and-monitoring.rst#topologytype-table
 */
void mongo::sdam::TopologyStateMachine::initTransitionTable() {
    // Init all cells to no-op
    for (auto topologyType : allTopologyTypes()) {
        _stt[topologyType] = StateTransitionTableRow{};
        for (auto serverType : allServerTypes()) {
            _stt[topologyType][serverType] = NO_OP;
        }
    }

    // From TopologyType: Unknown
    _stt[TopologyType::kUnknown][ServerType::kStandalone] =
        std::bind(&TopologyStateMachine::updateUnknownWithStandalone, this, _1, _2);
    _stt[TopologyType::kUnknown][ServerType::kMongos] = setTopologyType(TopologyType::kSharded);
    _stt[TopologyType::kUnknown][ServerType::kRSPrimary] =
        setTopologyTypeAndUpdateRSFromPrimary(TopologyType::kReplicaSetWithPrimary);

    {
        const auto serverTypes = std::vector<ServerType>{
            ServerType::kRSSecondary, ServerType::kRSArbiter, ServerType::kRSOther};
        for (auto newServerType : serverTypes) {
            _stt[TopologyType::kUnknown][newServerType] =
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
            _stt[TopologyType::kSharded][newServerType] =
                std::bind(&TopologyStateMachine::removeAndStopMonitoring, this, _1, _2);
        }
    }

    // From TopologyType: ReplicaSetNoPrimary
    {
        const auto serverTypes =
            std::vector<ServerType>{ServerType::kStandalone, ServerType::kMongos};
        for (auto serverType : serverTypes) {
            _stt[TopologyType::kReplicaSetNoPrimary][serverType] =
                std::bind(&TopologyStateMachine::removeAndStopMonitoring, this, _1, _2);
        }
    }

    _stt[TopologyType::kReplicaSetNoPrimary][ServerType::kRSPrimary] =
        setTopologyTypeAndUpdateRSFromPrimary(TopologyType::kReplicaSetWithPrimary);

    {
        const auto serverTypes = std::vector<ServerType>{
            ServerType::kRSSecondary, ServerType::kRSArbiter, ServerType::kRSOther};
        for (auto serverType : serverTypes) {
            _stt[TopologyType::kReplicaSetNoPrimary][serverType] =
                std::bind(&TopologyStateMachine::updateRSWithoutPrimary, this, _1, _2);
        }
    }

    // From TopologyType: ReplicaSetWithPrimary
    {
        const auto serverTypes =
            std::vector<ServerType>{ServerType::kUnknown, ServerType::kRSGhost};
        for (auto serverType : serverTypes) {
            _stt[TopologyType::kReplicaSetWithPrimary][serverType] =
                std::bind(&TopologyStateMachine::checkIfHasPrimary, this, _1, _2);
        }
    }

    {
        const auto serverTypes =
            std::vector<ServerType>{ServerType::kStandalone, ServerType::kMongos};
        for (auto serverType : serverTypes) {
            _stt[TopologyType::kReplicaSetNoPrimary][serverType] =
                std::bind(&TopologyStateMachine::removeAndCheckIfHasPrimary, this, _1, _2);
        }
    }

    _stt[TopologyType::kReplicaSetWithPrimary][ServerType::kRSPrimary] =
        std::bind(&TopologyStateMachine::updateRSFromPrimary, this, _1, _2);

    {
        const auto serverTypes = std::vector<ServerType>{
            ServerType::kRSSecondary, ServerType::kRSArbiter, ServerType::kRSOther};
        for (auto serverType : serverTypes) {
            _stt[TopologyType::kReplicaSetNoPrimary][serverType] =
                std::bind(&TopologyStateMachine::updateRSWithPrimaryFromMember, this, _1, _2);
        }
    }
}

void TopologyStateMachine::updateUnknownWithStandalone(TopologyDescription&,
                                                       const ServerDescription&) {}

void TopologyStateMachine::updateRSWithoutPrimary(TopologyDescription&, const ServerDescription&) {}

void TopologyStateMachine::updateRSWithPrimaryFromMember(TopologyDescription&,
                                                         const ServerDescription&) {}

void TopologyStateMachine::updateRSFromPrimary(TopologyDescription&, const ServerDescription&) {}

void TopologyStateMachine::removeAndStopMonitoring(TopologyDescription&, const ServerDescription&) {
}

void TopologyStateMachine::checkIfHasPrimary(TopologyDescription&, const ServerDescription&) {}

void TopologyStateMachine::removeAndCheckIfHasPrimary(TopologyDescription&,
                                                      const ServerDescription&) {}

TransitionAction TopologyStateMachine::setTopologyType(TopologyType type) {
    return [=](TopologyDescription& topologyDescription, const ServerDescription& newServerDescription) {
        topologyDescription.setType(type);
    };
}

TransitionAction TopologyStateMachine::setTopologyTypeAndUpdateRSFromPrimary(TopologyType type) {
    return [=](TopologyDescription& topologyDescription, const ServerDescription& newServerDescription) {
      topologyDescription.setType(type);
      updateRSFromPrimary(topologyDescription, newServerDescription);
    };
}

TransitionAction TopologyStateMachine::setTopologyTypeAndUpdateRSWithoutPrimary(TopologyType type) {
    return [=](TopologyDescription& topologyDescription, const ServerDescription& newServerDescription) {
      topologyDescription.setType(type);
      updateRSWithoutPrimary(topologyDescription, newServerDescription);
    };
}
}  // namespace mongo::sdam
