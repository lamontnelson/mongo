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
#include <string>
#include <unordered_set>

#include "boost/optional/optional.hpp"

#include "mongo/bson/oid.h"
#include "mongo/client/read_preference.h"
#include "mongo/client/sdam/sdam_datatypes.h"
#include "mongo/client/sdam/server_description.h"
#include "mongo/platform/basic.h"

namespace mongo::sdam {

class SdamConstants {};

class SdamConfiguration {
public:
    SdamConfiguration() : SdamConfiguration(boost::none){};
    SdamConfiguration(boost::optional<std::vector<ServerAddress>> seedList,
                      TopologyType initialType = TopologyType::kUnknown,
                      mongo::Milliseconds heartBeatFrequencyMs = mongo::Seconds(10),
                      boost::optional<std::string> setName = boost::none);

    const boost::optional<std::vector<ServerAddress>>& getSeedList() const;
    TopologyType getInitialType() const;
    Milliseconds getHeartBeatFrequency() const;
    Milliseconds getMinHeartbeatFrequencyMs() const;
    const boost::optional<std::string>& getSetName() const;

private:
    boost::optional<std::vector<ServerAddress>> _seedList;
    TopologyType _initialType;
    mongo::Milliseconds _heartBeatFrequencyMs;
    boost::optional<std::string> _setName;
    const mongo::Milliseconds _minHeartbeatFrequencyMS = mongo::Milliseconds(500);
};


class TopologyDescription {
public:
    TopologyDescription() : TopologyDescription(SdamConfiguration()) {}

    /**
     * From the Server Discovery & Monitoring Spec:
     * Initial Servers
     * The user MUST be able to set the initial servers list to a seed list of one or more
     * addresses.
     *
     * The hostname portion of each address MUST be normalized to lower-case.
     *
     * Initial TopologyType
     * The user MUST be able to set the initial TopologyType to Single.
     *
     * The user MAY be able to initialize it to ReplicaSetNoPrimary. This provides the user a way to
     * tell the client it can only connect to replica set members. Similarly the user MAY be able to
     * initialize it to Sharded, to connect only to mongoses.
     *
     * The user MAY be able to initialize it to Unknown, to allow for discovery of any topology type
     * based only on ismaster responses.
     *
     * The API for initializing TopologyType is not specified here. Drivers might already have a
     * convention, e.g. a single seed means Single, a setName means ReplicaSetNoPrimary, and a list
     * of seeds means Unknown. There are variations, however: In the Java driver a single seed means
     * Single, but a list containing one seed means Unknown, so it can transition to replica-set
     * monitoring if the seed is discovered to be a replica set member. In contrast, PyMongo
     * requires a non-null setName in order to begin replica-set monitoring, regardless of the
     * number of seeds. This spec does not imply existing driver APIs must change as long as all the
     * required features are somehow supported.
     *
     * Initial setName
     * The user MUST be able to set the client's initial replica set name. A driver MAY require the
     * set name in order to connect to a replica set, or it MAY be able to discover the replica set
     * name as it connects.
     *
     * Allowed configuration combinations
     * Drivers MUST enforce:
     *
     * TopologyType Single cannot be used with multiple seeds.
     * If setName is not null, only TopologyType ReplicaSetNoPrimary, and possibly Single, are
     * allowed. (See verifying setName with TopologyType Single.)
     */
    TopologyDescription(SdamConfiguration config);


    /**
     * Each time the client checks a server, it processes the outcome (successful or not) to create
     * a ServerDescription, and this method is called to update its TopologyDescription
     * @param newDescription
     */
    void onNewServerDescription(ServerDescription newDescription);

    /**
     * Determines if the topology has a readable server available.
     */
    bool hasReadableServer(
        boost::optional<ReadPreference> readPreference = ReadPreference::PrimaryOnly);

    /**
     * Determines if the topology has a writable server available.
     */
    bool hasWritableServer();

    const UUID& getId() const;
    TopologyType getType() const;
    const boost::optional<std::string>& getSetName() const;
    const boost::optional<int>& getMaxSetVersion() const;
    const boost::optional<OID>& getMaxElectionId() const;
    const std::vector<ServerDescription>& getServers() const;
    bool isWireVersionCompatible() const;
    const boost::optional<std::string>& getWireVersionCompatibleError() const;
    const boost::optional<int>& getLogicalSessionTimeoutMinutes() const;
    const Milliseconds& getHeartBeatFrequency() const;

    bool containsServerAddress(ServerAddress address) const;
    std::vector<ServerDescription> findServers(std::function<bool(const ServerDescription&)> predicate);

private:
    // unique id for this topology
    UUID _id = UUID::gen();

    // a TopologyType enum value.
    TopologyType _type = TopologyType::kUnknown;

public:
    void setType(TopologyType type);

private:
    // setName: the replica set name. Default null.
    boost::optional<std::string> _setName;

    // maxSetVersion: an integer or null. The largest setVersion ever reported by a primary.
    // Default null.
    boost::optional<int> _maxSetVersion;

    // maxElectionId: an ObjectId or null. The largest electionId ever reported by a primary.
    // Default null.
    boost::optional<OID> _maxElectionId;

    // servers: a set of ServerDescription instances. Default contains one server:
    // "localhost:27017", ServerType Unknown.
    std::vector<ServerDescription> _servers{
        ServerDescription("localhost:27017", ServerType::kUnknown)};

    // compatible: a boolean. False if any server's wire protocol version range is incompatible with
    // the client's. Default true.
    bool _compatible = true;

    // compatibilityError: a string. The error message if "compatible" is false, otherwise null.
    boost::optional<std::string> _compatibleError;

    // logicalSessionTimeoutMinutes: integer or null. Default null.
    boost::optional<int> _logicalSessionTimeoutMinutes;
};
}  // namespace mongo::sdam
