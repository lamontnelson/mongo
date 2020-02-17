#include "mongo/client/mongo_uri.h"
#include "mongo/client/sdam/sdam.h"
#include "mongo/executor/task_executor.h"
#include "mongo/stdx/unordered_map.h"
#include "mongo/util/net/hostandport.h"

namespace mongo {
using namespace sdam;

class SingleServerIsMasterMonitor
    : public std::enable_shared_from_this<SingleServerIsMasterMonitor> {
public:
    explicit SingleServerIsMasterMonitor(const MongoURI& setUri,
                                         const ServerAddress& host,
                                         Milliseconds heartbeatFrequencyMS,
                                         TopologyEventsPublisherPtr eventListener,
                                         std::shared_ptr<executor::TaskExecutor> executor);

    /**
     * Request an immediate check. The server will be checked immediately if we haven't completed
     * an isMaster less than SdamConfiguration::kMinHeartbeatFrequencyMS ago. Otherwise,
     * we schedule a check that runs after SdamConfiguration::kMinHeartbeatFrequencyMS since
     * the last isMaster.
     */
    void requestImmediateCheck();
    void disableExpeditedChecking();

    void init();
    void close();

private:
    void _scheduleNextIsMaster(WithLock, Milliseconds delay);
    void _doRemoteCommand();

    void _onIsMasterSuccess(IsMasterRTT latency, const BSONObj bson);
    void _onIsMasterFailure(IsMasterRTT latency, const Status& status, const BSONObj bson);

    Milliseconds _overrideRefreshPeriod(Milliseconds original);
    Milliseconds _currentRefreshPeriod(WithLock);
    void _cancelOutstandingRequest(WithLock);

    static const int kDebugLevel = 0;

    Mutex _mutex;
    ServerAddress _host;
    TopologyEventsPublisherPtr _eventListener;
    std::shared_ptr<executor::TaskExecutor> _executor;
    Milliseconds _heartbeatFrequencyMS;
    Milliseconds _timeoutMS = SdamConfiguration::kDefaultConnectTimeoutMS;

    boost::optional<Date_t> _lastIsMasterAt;
    bool _isMasterOutstanding = false;
    bool _isExpedited = false;
    executor::TaskExecutor::CallbackHandle _nextIsMasterHandle;
    executor::TaskExecutor::CallbackHandle _remoteCommandHandle;

    bool _isClosed;
    MongoURI _setUri;
};
using SingleServerIsMasterMonitorPtr = std::shared_ptr<SingleServerIsMasterMonitor>;


class ServerIsMasterMonitor : public TopologyListener {
public:
    ServerIsMasterMonitor(const MongoURI& setUri,
                          const SdamConfiguration& sdamConfiguration,
                          TopologyEventsPublisherPtr eventsPublisher,
                          TopologyDescriptionPtr initialTopologyDescription,
                          std::shared_ptr<executor::TaskExecutor> executor = nullptr);

    virtual ~ServerIsMasterMonitor() {}

    /**
     * Request an immediate check of each member in the replica set.
     */
    void requestImmediateCheck();

    void close();

    void onTopologyDescriptionChangedEvent(UUID topologyId,
                                           TopologyDescriptionPtr previousDescription,
                                           TopologyDescriptionPtr newDescription) override;

private:
    /**
     * If the provided executor exists, use that one (for testing). Otherwise create a new one.
     */
    std::shared_ptr<executor::TaskExecutor> _setupExecutor(
        const std::shared_ptr<executor::TaskExecutor>& executor);
    void _disableExpeditedChecking(WithLock);

    static const int kLogDebugLevel = 0;

    Mutex _mutex;
    SdamConfiguration _sdamConfiguration;
    TopologyEventsPublisherPtr _eventPublisher;
    std::shared_ptr<executor::TaskExecutor> _executor;
    std::unordered_map<ServerAddress, SingleServerIsMasterMonitorPtr> _singleMonitors;
    bool _isClosed;
    MongoURI _setUri;
};
using ServerIsMasterMonitorPtr = std::shared_ptr<ServerIsMasterMonitor>;
}  // namespace mongo
