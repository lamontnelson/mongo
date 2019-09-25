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
#pragma once

#include <unordered_map>

#include "mongo/client/sdam/server_description.h"
#include "mongo/client/sdam/topology_description.h"

namespace mongo::sdam {
using TransitionAction = std::function<void(TopologyDescription&, const ServerDescription&)>;
using StateTransitionTableRow = std::unordered_map<ServerType, TransitionAction>;
using StateTransitionTable = std::unordered_map<TopologyType, StateTransitionTableRow>;

class TopologyStateMachine {
public:
    TopologyStateMachine(const SdamConfiguration& config);

    // input to the state machine.
    // safe to call from multiple threads.
    void nextServerDescription(const ServerDescription& serverDescription);

private:
    void initTransitionTable();

    // transition actions
    static inline const TransitionAction NO_OP = TransitionAction();
    void updateUnknownWithStandalone(TopologyDescription&, const ServerDescription&);
    void updateRSWithoutPrimary(TopologyDescription&, const ServerDescription&);
    void updateRSWithPrimaryFromMember(TopologyDescription&, const ServerDescription&);
    void updateRSFromPrimary(TopologyDescription&, const ServerDescription&);
    void removeAndStopMonitoring(TopologyDescription&, const ServerDescription&);
    void checkIfHasPrimary(TopologyDescription&, const ServerDescription&);
    void removeAndCheckIfHasPrimary(TopologyDescription&, const ServerDescription&);
    TransitionAction setTopologyType(TopologyType t);
    TransitionAction setTopologyTypeAndUpdateRSFromPrimary(TopologyType type);
    TransitionAction setTopologyTypeAndUpdateRSWithoutPrimary(TopologyType type);

    StateTransitionTable _stt;
};
}  // namespace mongo::sdam
