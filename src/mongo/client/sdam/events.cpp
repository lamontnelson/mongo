// TODO: don't think we need... might want to do a subset of these though
///**
// * Published when server description changes, but does NOT include changes to the RTT.
// */
//interface ServerDescriptionChangedEvent {
//
//    /**
//     * Returns the address (host/port pair) of the server.
//     */
//    address: ServerAddress;
//
//    /**
//     * Returns a unique identifier for the topology.
//     */
//    topologyId: Object;
//
//    /**
//     * Returns the previous server description.
//     */
//    previousDescription: ServerDescription;
//
//    /**
//     * Returns the new server description.
//     */
//    newDescription: ServerDescription;
//}
//
///**
//  * Published when server is initialized.
//  */
//interface ServerOpeningEvent {
//
//    /**
//     * Returns the address (host/port pair) of the server.
//     */
//    address: ServerAddress;
//
//    /**
//     * Returns a unique identifier for the topology.
//     */
//    topologyId: Object;
//}
//
///**
//  * Published when server is closed.
//  */
//interface ServerClosedEvent {
//
//    /**
//     * Returns the address (host/port pair) of the server.
//     */
//    address: ServerAddress;
//
//    /**
//     * Returns a unique identifier for the topology.
//     */
//    topologyId: Object;
//}
//
///**
// * Published when topology description changes.
// */
//interface TopologyDescriptionChangedEvent {
//
//    /**
//     * Returns a unique identifier for the topology.
//     */
//    topologyId: Object;
//
//    /**
//     * Returns the old topology description.
//     */
//    previousDescription: TopologyDescription;
//
//    /**
//     * Returns the new topology description.
//     */
//    newDescription: TopologyDescription;
//}
//
///**
// * Published when topology is initialized.
// */
//interface TopologyOpeningEvent {
//
//    /**
//     * Returns a unique identifier for the topology.
//     */
//    topologyId: Object;
//}
//
///**
// * Published when topology is closed.
// */
//interface TopologyClosedEvent {
//
//    /**
//     * Returns a unique identifier for the topology.
//     */
//    topologyId: Object;
//}
//
///**
// * Fired when the server monitor’s ismaster command is started - immediately before
// * the ismaster command is serialized into raw BSON and written to the socket.
// */
//interface ServerHeartbeatStartedEvent {
//
//    /**
//      * Returns the connection id for the command. The connection id is the unique
//      * identifier of the driver’s Connection object that wraps the socket. For languages that
//      * do not have this object, this MUST a string of “hostname:port” or an object that
//      * that contains the hostname and port as attributes.
//      *
//      * The name of this field is flexible to match the object that is returned from the driver.
//      * Examples are, but not limited to, ‘address’, ‘serverAddress’, ‘connectionId’,
//      */
//    connectionId: ConnectionId;
//
//}
//
///**
// * Fired when the server monitor’s ismaster succeeds.
// */
//interface ServerHeartbeatSucceededEvent {
//
//    /**
//      * Returns the execution time of the event in the highest possible resolution for the platform.
//      * The calculated value MUST be the time to send the message and receive the reply from the server,
//      * including BSON serialization and deserialization. The name can imply the units in which the
//      * value is returned, i.e. durationMS, durationNanos. The time measurement used
//      * MUST be the same measurement used for the RTT calculation.
//      */
//    duration: Int64;
//
//    /**
//     * Returns the command reply.
//     */
//    reply: Document;
//
//    /**
//      * Returns the connection id for the command. For languages that do not have this,
//      * this MUST return the driver equivalent which MUST include the server address and port.
//      * The name of this field is flexible to match the object that is returned from the driver.
//      */
//    connectionId: ConnectionId;
//
//}
//
///**
// * Fired when the server monitor’s ismaster fails, either with an “ok: 0” or a socket exception.
// */
//interface ServerHeartbeatFailedEvent {
//
//    /**
//      * Returns the execution time of the event in the highest possible resolution for the platform.
//      * The calculated value MUST be the time to send the message and receive the reply from the server,
//      * including BSON serialization and deserialization. The name can imply the units in which the
//      * value is returned, i.e. durationMS, durationNanos.
//      */
//    duration: Int64;
//
//    /**
//      * Returns the failure. Based on the language, this SHOULD be a message string,
//      * exception object, or error document.
//      */
//    failure: String,Exception,Document;
//
//    /**
//      * Returns the connection id for the command. For languages that do not have this,
//      * this MUST return the driver equivalent which MUST include the server address and port.
//      * The name of this field is flexible to match the object that is returned from the driver.
//      */
//    connectionId: ConnectionId;
//}
namespace mongo::sdam {

};
