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

#include <vector>

#include "mongo/client/sdam/server_description.h"
#include "mongo/client/sdam/topology_description.h"
#include "mongo/client/sdam/topology_observer.h"
#include "mongo/platform/mutex.h"

namespace mongo::sdam {
using TransitionAction = std::function<void(TopologyDescription&, const ServerDescription&)>;

// indexed by ServerType
using StateTransitionTableRow = std::vector<TransitionAction>;

// StateTransitionTable[t][s] returns the action to
// take given that the topology currently has type t, and we receive a ServerDescription
// with type s.
using StateTransitionTable = std::vector<StateTransitionTableRow>;

class TopologyStateMachine {
public:
    TopologyStateMachine(const SdamConfiguration& config);


    // Provide input to the state machine, and triggers the correct action based on the current
    // TopologyDescription and the incoming ServerDescription. The topology may be modified as a
    // result. This is safe to call from multiple threads.
    void nextServerDescription(TopologyDescription& topologyDescription,
                               const ServerDescription& serverDescription);

    void addObserver(std::shared_ptr<TopologyObserver> observer);

private:
    void initTransitionTable();
    void addUnknownServers(const TopologyDescription& topologyDescription,
                           const ServerDescription& serverDescription);

    // transition actions
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

    void emitServerRemoved(const ServerDescription& serverDescription);
    void emitTypeChange(TopologyType topologyType);
    void emitNewSetName(const boost::optional<std::string>& setName);
    void emitNewServer(ServerDescription newServerDescription);
    void emitUpdateServerType(const ServerDescription& serverDescription, ServerType newServerType);
    void emitReplaceServer(ServerDescription newServerDescription);
    void emitNewMaxElectionId(const OID& newMaxElectionId);
    void emitNewMaxSetVersion(int& newMaxSetVersion);

    mongo::Mutex _mutex = mongo::Mutex(StringData("TopologyStateMachine"));
    StateTransitionTable _stt;
    SdamConfiguration _config;
    std::vector<std::shared_ptr<TopologyObserver>> _observers;
};
}  // namespace mongo::sdam
