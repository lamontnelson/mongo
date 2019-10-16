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
#include "mongo/client/sdam/topology_description.h"

namespace mongo::sdam {
enum class TopologyStateMachineEventType {
    kServerDescriptionChanged,
    kTopologyDescriptionChanged,
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
struct TopologyDescriptionChangeEvent : TopologyStateMachineEvent {
    TopologyDescriptionChangeEvent(const UUID& topologyId,
                                   const TopologyDescriptionPtr& previousTopologyDescription,
                                   const TopologyDescriptionPtr& newTopologyDescription);


    const UUID topologyId;
    const TopologyDescriptionPtr previousDescription;
    const TopologyDescriptionPtr newDescription;
};

struct ServerDescriptionChangeEvent : TopologyStateMachineEvent {
    /**
     * Called when when installing an existing ServerDescription
     */
    ServerDescriptionChangeEvent(const UUID& topologyId,
                                 const ServerDescriptionPtr& previousServerDescription,
                                 const ServerDescriptionPtr& newServerDescription);

    /*
     * Called when installing a new server description.
     *
     * From Monitoring Specification: "ServerDescriptions MUST be initialized with a default
     * description in an “unknown” state, guaranteeing that the previous description in the events
     * will never be null."
     *
     * https://github.com/mongodb/specifications/blob/master/source/server-discovery-and-monitoring/server-discovery-and-monitoring-monitoring.rst#initial-server-description
     */
    ServerDescriptionChangeEvent(const UUID& topologyId,
                                 const ServerDescriptionPtr& newServerDescription);

    const ServerAddress& address;
    const UUID topologyId;
    const ServerDescriptionPtr previousDescription;
    const ServerDescriptionPtr newDescription;
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
