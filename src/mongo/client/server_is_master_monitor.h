#include "mongo/client/sdam/sdam.h"
#include "mongo/executor/task_executor.h"
#include "mongo/stdx/unordered_map.h"
#include "mongo/util/net/hostandport.h"

namespace mongo {
class SingleServerIsMasterMonitor
    : public std::enable_shared_from_this<SingleServerIsMasterMonitor> {
public:
    SingleServerIsMasterMonitor(const sdam::ServerAddress& host,
                                Milliseconds heartbeatFrequencyMS,
                                sdam::TopologyEventsPublisherPtr eventListener,
                                std::shared_ptr<executor::TaskExecutor> executor);
    void init();
    void close();

    virtual ~SingleServerIsMasterMonitor() {
        std::cout << "destroy SingleServerIsMasterMonitor";
    }


private:
    void _scheduleNextIsMaster(Milliseconds delay);
    void _doRemoteCommand();

    void _onIsMasterSuccess(sdam::IsMasterRTT latency, const BSONObj bson);
    void _onIsMasterFailure(sdam::IsMasterRTT latency, const Status& status, const BSONObj bson);

    static const int kDebugLevel = 0;

    Mutex _mutex;
    sdam::ServerAddress _host;
    sdam::TopologyEventsPublisherPtr _eventListener;
    std::shared_ptr<executor::TaskExecutor> _executor;
    Milliseconds _heartbeatFrequencyMS;
    Milliseconds _timeoutMS = Milliseconds(static_cast<int64_t>(10));

    executor::TaskExecutor::CallbackHandle _nextIsMasterHandle;
    executor::TaskExecutor::CallbackHandle _remoteCommandHandle;
    bool _isClosed;
};
using SingleServerIsMasterMonitorPtr = std::shared_ptr<SingleServerIsMasterMonitor>;


class ServerIsMasterMonitor : public sdam::TopologyListener {
public:
    ServerIsMasterMonitor(const sdam::SdamConfiguration& sdamConfiguration,
                          sdam::TopologyEventsPublisherPtr eventsPublisher,
                          sdam::TopologyDescriptionPtr initialTopologyDescription,
                          std::shared_ptr<executor::TaskExecutor> executor = nullptr);

    virtual ~ServerIsMasterMonitor() {
        std::cout << "destroy ServerIsMasterMonitor";
    }

    void close();

    void onTopologyDescriptionChangedEvent(UUID topologyId,
                                           sdam::TopologyDescriptionPtr previousDescription,
                                           sdam::TopologyDescriptionPtr newDescription) override;

private:
    /**
     * If the provided executor exists, use that one (for testing). Otherwise create a new one.
     */
    std::shared_ptr<executor::TaskExecutor> _setupExecutor(
        const std::shared_ptr<executor::TaskExecutor>& executor);
    static const int kLogDebugLevel = 0;

    Mutex _mutex;
    sdam::SdamConfiguration _sdamConfiguration;
    sdam::TopologyEventsPublisherPtr _eventPublisher;
    std::shared_ptr<executor::TaskExecutor> _executor;
    std::unordered_map<sdam::ServerAddress, SingleServerIsMasterMonitorPtr> _singleMonitors;
    bool _isClosed;
};
using ServerIsMasterMonitorPtr = std::shared_ptr<ServerIsMasterMonitor>;
}  // namespace mongo
