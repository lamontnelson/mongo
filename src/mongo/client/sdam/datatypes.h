#pragma once
#include <chrono>
#include <string>

#include "boost/optional.hpp"

#include "mongo/bson/bsonobj.h"
#include "mongo/util/duration.h"

namespace mongo::sdam {
enum class TopologyType { Single, ReplicaSetNoPrimary, ReplicaSetWithPrimary, Sharded, Unknown };

enum class ServerType {
    Standalone,
    Mongos,
    RSPrimary,
    RSSecondary,
    RSArbiter,
    RSOther,
    RSGhost,
    Unknown
};

using ServerAddress = std::string;
using OpLatency = mongo::Nanoseconds;

// The result of an attempt to call the "ismaster" command on a server.
struct IsMasterOutcome {
    IsMasterOutcome() = delete;
    static IsMasterOutcome forSuccess(const ServerAddress& server,
                                      const BSONObj& response,
                                      OpLatency latency);
    static IsMasterOutcome forFailure(const ServerAddress& server);

    ServerAddress server;
    // indicating the success or failure of the attempt
    bool success;
    // a document containing the command response (or boost::none if it failed)
    boost::optional<BSONObj> response;
    // the round trip time to execute the command (or null if it failed)
    boost::optional<OpLatency> latency;
};
};  // namespace mongo::sdam
