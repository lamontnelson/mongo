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
#include "mongo/util/uuid.h"

namespace mongo::sdam {

/**
 * Event listener interface for the Server Discovery and Monitoring (SDAM) sub-system.
 * This interface is defined in
 * https://github.com/mongodb/specifications/blob/03aed6c58dcd6afd81980876be2042afc45d06d3/source/server-discovery-and-monitoring/server-discovery-and-monitoring-monitoring.rst#specification
 */
class TopologyListener {
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
        TopologyDescriptionPtr newDescription) = 0;

    /**
     * Published when server description changes, but does NOT include changes to the RTT.
     */
    virtual void onServerDescriptionChangedEvent(

        /**
         * Returns the address (host/port pair) of the server.
         */
        ServerAddress serverAddress,

        /**
         * Returns a unique identifier for the topology.
         */
        UUID topologyId,

        /**
         * Returns the previous server description.
         */
        ServerDescriptionPtr previousDescription,

        /**
         * Returns the new server description.
         */
        ServerDescriptionPtr newDescription) = 0;

    /**
     * Published when server is initialized.
     */
    virtual void onServerOpeningEvent(
        /**
         * Returns the address (host/port pair) of the server.
         */
        ServerAddress address,

        /**
         * Returns a unique identifier for the topology.
         */
        UUID topologyId) = 0;

    /**
     * Published when server is closed.
     */
    virtual void onServerClosedEvent(
        /**
         * Returns the address (host/port pair) of the server.
         */
        ServerAddress address,

        /**
         * Returns a unique identifier for the topology.
         */
        UUID topologyId) = 0;

    /**
     * Published when topology is initialized.
     */
    virtual void onTopologyOpeningEvent(
        /**
         * Returns a unique identifier for the topology.
         */
        UUID topologyId) = 0;


    /**
     * Published when topology is closed.
     */
    virtual void onTopologyClosedEvent(
        /**
         * Returns a unique identifier for the topology.
         */
        UUID topologyId) = 0;

    /**
     * Fired when the server monitor’s ismaster command is started - immediately before
     * the ismaster command is serialized into raw BSON and written to the socket.
     */
    virtual void onServerHeartbeatStartedEvent(
        /**
         * Returns the connection id for the command. The connection id is the unique
         * identifier of the driver’s Connection object that wraps the socket. For languages that
         * do not have this object, this MUST a string of “hostname:port” or an object that
         * that contains the hostname and port as attributes.
         *
         * The name of this field is flexible to match the object that is returned from the driver.
         * Examples are, but not limited to, ‘address’, ‘serverAddress’, ‘connectionId’,
         */
        ServerAddress address) = 0;

    /**
     * Fired when the server monitor’s ismaster succeeds.
     */
    virtual void onServerHeartbeatSucceededEvent(
        /**
         * Returns the execution time of the event in the highest possible resolution for the
         * platform. The calculated value MUST be the time to send the message and receive the reply
         * from the server, including BSON serialization and deserialization. The name can imply the
         * units in which the value is returned, i.e. durationMS, durationNanos. The time
         * measurement used MUST be the same measurement used for the RTT calculation.
         */
        IsMasterRTT duration,

        /**
         * Returns the connection id for the command. For languages that do not have this,
         * this MUST return the driver equivalent which MUST include the server address and port.
         * The name of this field is flexible to match the object that is returned from the driver.
         */
        ServerAddress serverAddress) = 0;

    /**
     * Fired when the server monitor’s ismaster fails, either with an “ok: 0” or a socket exception.
     */
    virtual void onServerHeartbeatFailedEvent(
        /**
         * Returns the execution time of the event in the highest possible resolution for the
         * platform. The calculated value MUST be the time to send the message and receive the reply
         * from the server, including BSON serialization and deserialization. The name can imply the
         * units in which the value is returned, i.e. durationMS, durationNanos.
         */
        IsMasterRTT duration,

        /**
         * Returns the failure. Based on the language, this SHOULD be a message string,
         * exception object, or error document.
         */
        StatusWith<BSONObj> failure,

        /**
         * Returns the connection id for the command. For languages that do not have this,
         * this MUST return the driver equivalent which MUST include the server address and port.
         * The name of this field is flexible to match the object that is returned from the driver.
         */
        ServerAddress serverAddress) = 0;
};

/**
 * Convenience class that allows Listeners to only have to implement the events they are interested
 * in.
 */
class NoOpTopologyListener : public TopologyListener {
    void onTopologyDescriptionChangedEvent(UUID topologyId,
                                           TopologyDescriptionPtr previousDescription,
                                           TopologyDescriptionPtr newDescription) override;
    void onServerDescriptionChangedEvent(ServerAddress serverAddress,
                                         UUID topologyId,
                                         ServerDescriptionPtr previousDescription,
                                         ServerDescriptionPtr newDescription) override;
    void onServerOpeningEvent(ServerAddress address, UUID topologyId) override;
    void onServerClosedEvent(ServerAddress address, UUID topologyId) override;
    void onTopologyOpeningEvent(UUID topologyId) override;
    void onTopologyClosedEvent(UUID topologyId) override;
    void onServerHeartbeatStartedEvent(ServerAddress address) override;
    void onServerHeartbeatSucceededEvent(IsMasterRTT duration,
                                         ServerAddress serverAddress) override;
    void onServerHeartbeatFailedEvent(IsMasterRTT duration,
                                      StatusWith<BSONObj> failure,
                                      ServerAddress serverAddress) override;
};
}  // namespace mongo::sdam
