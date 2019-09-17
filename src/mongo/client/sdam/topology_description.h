#pragma once
#include <string>
#include <unordered_set>

#include "boost/optional/optional.hpp"

#include "mongo/bson/oid.h"
#include "mongo/client/read_preference.h"
#include "mongo/client/sdam/datatypes.h"
#include "mongo/client/sdam/server_description.h"
#include "mongo/platform/basic.h"

namespace mongo::sdam {

//class ServerCheckError {
//    ServerCheckError() = delete;
//
//public:
//    ServerCheckError(const ServerAddress serverAddress, const mongo::Status errorStatus);
//    const ServerAddress& getServerAddress() const;
//    const Status& getErrorStatus() const;
//
//private:
//    ServerAddress _serverAddress;
//    Status _errorStatus;
//};

class TopologyDescription {
    TopologyDescription() = default;

    /**
     * Initial Servers
     *The user MUST be able to set the initial servers list to a seed list of one or more addresses.
     *
     *The hostname portion of each address MUST be normalized to lower-case.
     *
     *Initial TopologyType
     *The user MUST be able to set the initial TopologyType to Single.
     *
     *The user MAY be able to initialize it to ReplicaSetNoPrimary. This provides the user a way to
     *tell the client it can only connect to replica set members. Similarly the user MAY be able to
     *initialize it to Sharded, to connect only to mongoses.
     *
     *The user MAY be able to initialize it to Unknown, to allow for discovery of any topology type
     *based only on ismaster responses.
     *
     *The API for initializing TopologyType is not specified here. Drivers might already have a
     *convention, e.g. a single seed means Single, a setName means ReplicaSetNoPrimary, and a list
     *of seeds means Unknown. There are variations, however: In the Java driver a single seed means
     *Single, but a list containing one seed means Unknown, so it can transition to replica-set
     *monitoring if the seed is discovered to be a replica set member. In contrast, PyMongo requires
     *a non-null setName in order to begin replica-set monitoring, regardless of the number of
     *seeds. This spec does not imply existing driver APIs must change as long as all the required
     *features are somehow supported.
     *
     *Initial setName
     *The user MUST be able to set the client's initial replica set name. A driver MAY require the
     *set name in order to connect to a replica set, or it MAY be able to discover the replica set
     *name as it connects.
     *
     *Allowed configuration combinations
     *Drivers MUST enforce:
     *
     *TopologyType Single cannot be used with multiple seeds.
     *If setName is not null, only TopologyType ReplicaSetNoPrimary, and possibly Single, are
     *allowed. (See verifying setName with TopologyType Single.)
     */
    TopologyDescription(TopologyType topologyType,
                        std::vector<ServerAddress> seedList,
                        StringData setName);


    /**
     * Each time the client checks a server, it processes the outcome (successful or not) to create
     * a ServerDescription, and this method is called to update its TopologyDescription
     * @param newDescription
     */
    void onNewServerDescription(ServerDescription newDescription);

//    /**
//     * Occurs if there is an error while processing an ismaster request for a server.
//     * @param error
//     */
//    void onServerCheckError(ServerCheckError error);


    /**
     * Determines if the topology has a readable server available.
     */
    bool hasReadableServer(
        boost::optional<ReadPreference> readPreference = ReadPreference::PrimaryOnly);

    /**
     * Determines if the topology has a writable server available.
     */
    bool hasWritableServer();

private:
    stdx::mutex _mutex;

    // unique id for this topology
    UUID _id = UUID::gen();

    // a TopologyType enum value.
    TopologyType _type = TopologyType::Unknown;

    // setName: the replica set name. Default null.
    boost::optional<std::string> _setName;

    //    maxSetVersion: an integer or null. The largest setVersion ever reported by a primary.
    //    Default null.
    boost::optional<int> _maxSetVersion;

    // maxElectionId: an ObjectId or null. The largest electionId ever reported by a primary.
    // Default null.
    boost::optional<OID> _maxElectionId;

    // servers: a set of ServerDescription instances. Default contains one server:
    // "localhost:27017", ServerType Unknown.
    std::vector<ServerDescription> _servers{
        ServerDescription("localhost:27017", ServerType::Unknown)};

    // compatible: a boolean. False if any server's wire protocol version range is incompatible with
    // the client's. Default true.
    bool _compatible = true;

    // compatibilityError: a string. The error message if "compatible" is false, otherwise null.
    boost::optional<std::string> _compatibleError;

    // logicalSessionTimeoutMinutes: integer or null. Default null. See logical session timeout.
    boost::optional<int> _logicalSessionTimeoutMinutes;
};
}  // namespace mongo::sdam
