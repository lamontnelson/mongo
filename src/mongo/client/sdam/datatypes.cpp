#include "mongo/client/sdam/datatypes.h"

namespace mongo::sdam {
IsMasterOutcome IsMasterOutcome::forSuccess(const ServerAddress& server,
                                            const BSONObj& response,
                                            OpLatency latency) {
    return IsMasterOutcome{server, true, response, latency};
}

IsMasterOutcome IsMasterOutcome::forFailure(const ServerAddress& server) {
    return IsMasterOutcome{server, false, boost::none, boost::none};
}
};  // namespace mongo::sdam
