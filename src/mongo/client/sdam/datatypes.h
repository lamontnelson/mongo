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
class IsMasterOutcome {
    IsMasterOutcome() = delete;
public:
    // success constrructor
    IsMasterOutcome(ServerAddress server, BSONObj response, OpLatency rtt)
        : _server(std::move(server)), _success(true), _response(response), _rtt(rtt) {}

    // failure constructor
    IsMasterOutcome(ServerAddress server, std::string errorMsg)
        : _server(std::move(server)), _success(false), _errorMsg(errorMsg) {}

    const ServerAddress& getServer() const;
    bool isSuccess() const;
    const boost::optional<BSONObj>& getResponse() const;
    const boost::optional<OpLatency>& getRtt() const;
    const std::string& getErrorMsg() const;

private:
    ServerAddress _server;
    // indicating the success or failure of the attempt
    bool _success;
    // an error message in case of failure
    std::string _errorMsg;
    // a document containing the command response (or boost::none if it failed)
    boost::optional<BSONObj> _response;
    // the round trip time to execute the command (or null if it failed)
    boost::optional<OpLatency> _rtt;
};
};  // namespace mongo::sdam
