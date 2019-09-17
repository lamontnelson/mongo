#include "mongo/client/sdam/topology_description.h"

#include "mongo/platform/basic.h"
namespace mongo::sdam {
//const ServerAddress& ServerCheckError::getServerAddress() const {
//    return _serverAddress;
//}
//
//
//const mongo::Status& ServerCheckError::getErrorStatus() const {
//    return _errorStatus;
//}
//
//
//ServerCheckError::ServerCheckError(const ServerAddress serverAddress,
//                                   const mongo::Status errorStatus)
//    : _serverAddress(serverAddress), _errorStatus(errorStatus) {}


TopologyDescription::TopologyDescription(TopologyType topologyType,
                                         std::vector<ServerAddress> seedList,
                                         StringData setName) {}

void TopologyDescription::onNewServerDescription(ServerDescription newDescription) {
    stdx::lock_guard<stdx::mutex> lock(_mutex);
    // TODO:
}


// TopologyDescription TopologyDescription::onServerCheckError(ServerCheckError error) {
//    // calculate new ServerDescription
//    // call onNewServerDescription
//    ServerDescription serverDescription(error.getServerAddress(), ServerType::Unknown);
//    return onNewServerDescription(serverDescription)
//}


bool TopologyDescription::hasReadableServer(boost::optional<ReadPreference> readPreference) {
    return false;
}


bool TopologyDescription::hasWritableServer() {
    return false;
}

};  // namespace mongo::sdam
