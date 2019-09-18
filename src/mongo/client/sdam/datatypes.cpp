#include "mongo/client/sdam/datatypes.h"

namespace mongo::sdam {
std::string toString(ServerType serverType) {
    switch (serverType) {
        case ServerType::Standalone:
            return "Standalone";
        case ServerType::Mongos:
            return "Mongos";
        case ServerType::RSPrimary:
            return "RSPrimary";
        case ServerType::RSSecondary:
            return "RSSecondary";
        case ServerType::RSArbiter:
            return "RSArbiter";
        case ServerType::RSOther:
            return "RSOther";
        case ServerType::RSGhost:
            return "RSGhost";
        case ServerType::Unknown:
            return "Unknown";
        default:
            MONGO_UNREACHABLE;
    }
}


const ServerAddress& IsMasterOutcome::getServer() const {
    return _server;
}
bool IsMasterOutcome::isSuccess() const {
    return _success;
}
const boost::optional<BSONObj>& IsMasterOutcome::getResponse() const {
    return _response;
}
const boost::optional<OpLatency>& IsMasterOutcome::getRtt() const {
    return _rtt;
}
const std::string& IsMasterOutcome::getErrorMsg() const {
    return _errorMsg;
}
};  // namespace mongo::sdam
