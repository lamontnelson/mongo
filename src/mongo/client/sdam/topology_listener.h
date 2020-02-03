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
#include "mongo/executor/task_executor.h"
#include "mongo/util/uuid.h"

namespace mongo::sdam {
class TopologyListener {
public:
    virtual ~TopologyListener() {}
    /**
     * Published when topology description changes.
     */
    virtual void onTopologyDescriptionChangedEvent(
        /**
         * Returns a unique identifier for the topology.
         */
        UUID topologyId,

        /**
         * Returns the old topology description.
         */
        TopologyDescriptionPtr previousDescription,

        /**
         * Returns the new topology description.
         */
        TopologyDescriptionPtr newDescription){};

    virtual void onServerHeartbeatSucceededEvent(
        /**
         * Returns the execution time of the event in the highest possible resolution for the
         * platform. The calculated value MUST be the time to send the message and receive the reply
         * from the server, including BSON serialization and deserialization. The name can imply the
         * units in which the value is returned, i.e. durationMS, durationNanos. The time
         * measurement used MUST be the same measurement used for the RTT calculation.
         */
        mongo::Milliseconds durationMs,

        /**
         * Returns the connection id for the command. For languages that do not have this,
         * this MUST return the driver equivalent which MUST include the server address and port.
         * The name of this field is flexible to match the object that is returned from the driver.
         */
        ServerAddress hostAndPort,

        const BSONObj reply){};

    virtual void onServerHeartbeatFailureEvent(mongo::Milliseconds durationMs,
                                               Status errorStatus,
                                               ServerAddress hostAndPort,
                                               const BSONObj reply){};

    virtual void onServerPingFailedEvent(const ServerAddress hostAndPort, const Status& status){};

    virtual void onServerPingSucceededEvent(mongo::Milliseconds durationMS,
                                            ServerAddress hostAndPort){};
};

/**
 * This class publishes TopologyListener events to a group of registered listeners.
 *
 * To publish an event to all registered listeners call the corresponding event function on the
 * TopologyEventsPublisher instance.
 */
class TopologyEventsPublisher : public TopologyListener,
                                public std::enable_shared_from_this<TopologyEventsPublisher> {
public:
    TopologyEventsPublisher(std::shared_ptr<executor::TaskExecutor> executor)
        : _executor(executor){};
    void registerListener(TopologyListenerPtr listener);
    void removeListener(TopologyListenerPtr listener);
    void close();

    virtual ~TopologyEventsPublisher() {}

    void onTopologyDescriptionChangedEvent(UUID topologyId,
                                           TopologyDescriptionPtr previousDescription,
                                           TopologyDescriptionPtr newDescription) override;
    void onServerHeartbeatSucceededEvent(mongo::Milliseconds durationMs,
                                         ServerAddress hostAndPort,
                                         const BSONObj reply) override;
    void onServerHeartbeatFailureEvent(mongo::Milliseconds durationMs,
                                       Status errorStatus,
                                       ServerAddress hostAndPort,
                                       const BSONObj reply) override;
    void onServerPingFailedEvent(const ServerAddress hostAndPort, const Status& status) override;
    void onServerPingSucceededEvent(mongo::Milliseconds durationMS,
                                    ServerAddress hostAndPort) override;


private:
    enum class EventType {
        HEARTBEAT_SUCCESS,
        HEARTBEAT_FAILURE,
        PING_SUCCESS,
        PING_FAILURE,
        TOPOLOGY_DESCRIPTION_CHANGED
    };
    struct Event {
        EventType type;
        ServerAddress hostAndPort;
        IsMasterRTT duration;
        BSONObj reply;
        TopologyDescriptionPtr previousDescription;
        TopologyDescriptionPtr newDescription;
        boost::optional<UUID> topologyId;
        Status status = Status::OK();
    };
    using EventPtr = std::unique_ptr<Event>;

    void _sendEvent(TopologyListenerPtr listener, const TopologyEventsPublisher::Event& event);
    void nextDelivery();
    void _scheduleNextDelivery();

    // Lock acquisition order to avoid deadlock is _eventQueueMutex -> _mutex
    Mutex _eventQueueMutex;
    std::deque<EventPtr> _eventQueue;

    Mutex _mutex;
    bool _isClosed = false;
    std::shared_ptr<executor::TaskExecutor> _executor;
    std::vector<TopologyListenerPtr> _listeners;
};
using TopologyEventsPublisherPtr = std::shared_ptr<TopologyEventsPublisher>;
}  // namespace mongo::sdam
