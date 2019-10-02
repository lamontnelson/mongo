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
 *  1. To start the process of collecting data from the cluster nodes via ismaster requests.
 *  2. To initiate state transitions of the topology description based on new ServerDescriptions
 *  3. To respond to requests about the current state of the cluster.
 */
class TopologyManager {
public:
    TopologyManager(executor::TaskExecutor* taskExecutor, const SdamConfiguration& config);
    void onServerDescription(const ServerDescription& description);

    // observers shouldn't do any blocking operations as they are called from
    // the stateMachine transition functions. Currently used by the TopologyManager and tests.
    void addObserver(std::shared_ptr<TopologyObserver> observer);

private:
    executor::TaskExecutor* _taskExecutor = nullptr;
    TopologyDescription _topologyDescription;
    TopologyStateMachine _stateMachine;

    friend class TopologyManagerObserver;
};

class TopologyManagerObserver : public TopologyObserver {
    TopologyManagerObserver() = delete;
public:
    TopologyManagerObserver(TopologyManager* manager);
    void onTypeChange(TopologyType topologyType) override;
    void onNewSetName(boost::optional<std::string> setName) override;
    void onUpdatedServerType(const ServerDescription& serverDescription,
                             ServerType newServerType) override;
    void onNewMaxElectionId(const OID& newMaxElectionId) override;
    void onNewMaxSetVersion(int newMaxSetVersion) override;
    void onNewServerDescription(const ServerDescription& newServerDescription) override;
    void onUpdateServerDescription(const ServerDescription& newServerDescription) override;
    void onServerDescriptionRemoved(const ServerDescription& serverDescription) override;

private:
    TopologyManager* _manager;
};
}  // namespace mongo::sdam
