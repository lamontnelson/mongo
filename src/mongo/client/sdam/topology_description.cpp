#include "mongo/client/sdam/topology_description.h"

#include "mongo/platform/basic.h"

namespace mongo::sdam {
TopologyDescription::TopologyDescription(TopologyType topologyType,
                                         std::vector<ServerAddress> seedList,
                                         StringData setName) {}

void TopologyDescription::onNewServerDescription(ServerDescription newDescription) {
    stdx::lock_guard<stdx::mutex> lock(_mutex);
    // TODO:
}

bool TopologyDescription::hasReadableServer(boost::optional<ReadPreference> readPreference) {
    return false;
}

bool TopologyDescription::hasWritableServer() {
    return false;
}
};  // namespace mongo::sdam
