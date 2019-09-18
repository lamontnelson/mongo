#include "mongo/client/sdam/datatypes.h"

namespace mongo::sdam {
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
