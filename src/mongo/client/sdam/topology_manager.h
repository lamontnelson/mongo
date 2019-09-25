#pragma once
#include <memory>

#include "mongo/client/sdam/topology_description.h"
#include "mongo/client/sdam/topology_state_machine.h"
#include "mongo/executor/non_auth_task_executor.h"
#include "mongo/executor/task_executor.h"
#include "mongo/platform/basic.h"

namespace mongo::sdam {
/**
 * This class is the public interface to the Server Discovery And Monitoring (SDAM) sub-system.
 * It has three responsibilities:
 *  1. To start collecting data from the cluster nodes via ismaster requests.
 *  2. To initiate state transitions of the topology description based on new ServerDescriptions
 *  3. To respond to requests about the current state of the cluster.
 */
class TopologyMangager {
public:
    TopologyMangager(executor::TaskExecutor* taskExecutor, const SdamConfiguration& config);
    void onServerDescription(const ServerDescription& description);

private:
    executor::TaskExecutor* _taskExecutor = nullptr;
    TopologyDescription _topologyDescription;
    TopologyStateMachine _stateMachine;
};
}  // namespace mongo::sdam
