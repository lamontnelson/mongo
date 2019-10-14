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

#include "mongo/client/sdam/sdam_datatypes.h"
#include "mongo/client/sdam/server_description.h"

namespace mongo::sdam {
enum class TopologyStateMachineEventType {
    kTopologyTypeChange,
    kNewSetName,
    kNewMaxElectionId,
    kNewMaxSetVersion,
    kNewServerDescription,
    kUpdateServerDescription,
    kRemoveServerDescription
};

/**
 * Base type for events emitted by the state machine
 */
struct TopologyStateMachineEvent {
    explicit TopologyStateMachineEvent(TopologyStateMachineEventType type);
    virtual ~TopologyStateMachineEvent() = default;
    const TopologyStateMachineEventType type;
};

/**
 * Concrete event types for the state machine
 */

struct TopologyTypeChangeEvent : public TopologyStateMachineEvent {
    explicit TopologyTypeChangeEvent(TopologyType newType);
    const TopologyType newType;
};

struct NewSetNameEvent : public TopologyStateMachineEvent {
    explicit NewSetNameEvent(boost::optional<std::string> newSetName);
    const boost::optional<std::string> newSetName;
};

struct NewMaxElectionIdEvent : public TopologyStateMachineEvent {
    explicit NewMaxElectionIdEvent(const OID& newMaxElectionId);
    OID newMaxElectionId;
};

struct NewMaxSetVersionEvent : public TopologyStateMachineEvent {
    explicit NewMaxSetVersionEvent(int newMaxSetVersion);
    int newMaxSetVersion;
};

struct NewServerDescriptionEvent : public TopologyStateMachineEvent {
    explicit NewServerDescriptionEvent(ServerDescription updatedServerDescription);
    ServerDescription newServerDescription;
};

struct UpdateServerDescriptionEvent : public TopologyStateMachineEvent {
    explicit UpdateServerDescriptionEvent(ServerDescription updatedServerDescription);
    ServerDescription updatedServerDescription;
};

struct RemoveServerDescriptionEvent : public TopologyStateMachineEvent {
    RemoveServerDescriptionEvent(ServerDescription removedServerDescription);
    ServerDescription removedServerDescription;
};

/**
 * Classes interested in state machine events should inherit from this and check for the event type
 * in e->type.
 */
class TopologyObserver {
public:
    virtual void onTopologyStateMachineEvent(std::shared_ptr<TopologyStateMachineEvent> e) = 0;
};
}  // namespace mongo::sdam
