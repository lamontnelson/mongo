#include "mongo/client/server_is_master_monitor.h"

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault
#include "mongo/executor/network_interface_factory.h"
#include "mongo/executor/network_interface_thread_pool.h"
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/rpc/metadata/egress_metadata_hook_list.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {
const BSONObj IS_MASTER_BSON = BSON("isMaster" << 1);

using executor::NetworkInterface;
using executor::NetworkInterfaceThreadPool;
using executor::TaskExecutor;
using executor::ThreadPoolTaskExecutor;
}  // namespace

SingleServerIsMasterMonitor::SingleServerIsMasterMonitor(
    const sdam::ServerAddress& host,
    Milliseconds heartbeatFrequencyMS,
    sdam::TopologyEventsPublisherPtr eventListener,
    std::shared_ptr<executor::TaskExecutor> executor)
    : _host(host),
      _eventListener(eventListener),
      _executor(executor),
      _heartbeatFrequencyMS(heartbeatFrequencyMS) {
    LOG(kDebugLevel) << "Created Replica Set SingleServerIsMasterMonitor for host " << host;
}

void SingleServerIsMasterMonitor::init() {
    {
        stdx::lock_guard<Mutex> lk(_mutex);
        _active = true;
    }

    _scheduleNextIsMaster();
}

void SingleServerIsMasterMonitor::_scheduleNextIsMaster() {
    stdx::lock_guard<Mutex> lk(_mutex);
    if (!_active)
        return;

    Timer timer;
    auto swCbHandle = _executor->scheduleWorkAt(
        _executor->now() + _heartbeatFrequencyMS,
        [self = shared_from_this()](const executor::TaskExecutor::CallbackArgs& cbData) {
            self->_doRemoteCommand();
        });

    if (!swCbHandle.isOK()) {
        mongo::Microseconds latency(timer.micros());
        _onIsMasterFailure(latency, swCbHandle.getStatus(), BSONObj());
        return;
    }

    _nextIsMasterHandle = swCbHandle.getValue();
}

void SingleServerIsMasterMonitor::_doRemoteCommand() {
    auto request = executor::RemoteCommandRequest(
        HostAndPort(_host), "admin", IS_MASTER_BSON, nullptr, _timeoutMS);
    // request.sslMode = _set->setUri.getSSLMode();
    stdx::lock_guard<Mutex> lk(_mutex);
    if (!_active)
        return;

    Timer timer;
    auto swCbHandle = _executor->scheduleRemoteCommand(
        std::move(request),
        [self = shared_from_this(),
         timer](const executor::TaskExecutor::RemoteCommandCallbackArgs& result) mutable {
            {
                stdx::lock_guard<Mutex> lk(self->_mutex);
                if (!self->_active)
                    return;

                Microseconds latency(timer.micros());
                if (result.response.isOK()) {
                    self->_onIsMasterSuccess(latency, result.response.data);
                } else {
                    self->_onIsMasterFailure(latency, result.response.status, result.response.data);
                }
            }

            self->_scheduleNextIsMaster();
        });

    if (!swCbHandle.isOK()) {
        mongo::Microseconds latency(timer.micros());
        _onIsMasterFailure(latency, swCbHandle.getStatus(), BSONObj());
        return;
    }

    _remoteCommandHandle = swCbHandle.getValue();
}

void SingleServerIsMasterMonitor::close() {
    stdx::lock_guard<Mutex> lk(_mutex);
    LOG(kDebugLevel) << "Closing Replica Set SingleServerIsMasterMonitor for host " << _host;
    _active = false;

    if (_nextIsMasterHandle.isValid()) {
        _executor->cancel(_nextIsMasterHandle);
    }

    if (_remoteCommandHandle.isValid()) {
        _executor->cancel(_remoteCommandHandle);
    }

    _executor = nullptr;
}

void SingleServerIsMasterMonitor::_onIsMasterSuccess(sdam::IsMasterRTT latency,
                                                     const BSONObj bson) {
    LOG(kDebugLevel) << "received isMaster for server " << _host << " (" << latency << ")";
    LOG(kDebugLevel) << _host << "; " << bson.toString();
    _eventListener->onServerHeartbeatSucceededEvent(
        duration_cast<Milliseconds>(latency), _host, bson);
}

void SingleServerIsMasterMonitor::_onIsMasterFailure(sdam::IsMasterRTT latency,
                                                     const Status& status,
                                                     const BSONObj bson) {
    LOG(kDebugLevel) << "received isMaster failure for server " << _host << ": "
                     << status.toString();
    LOG(kDebugLevel) << _host << "; " << bson.toString();
    _eventListener->onServerHeartbeatFailureEvent(
        duration_cast<Milliseconds>(latency), status, _host, bson);
}


ServerIsMasterMonitor::ServerIsMasterMonitor(
    const sdam::SdamConfiguration& sdamConfiguration,
    sdam::TopologyEventsPublisherPtr eventsPublisher,
    sdam::TopologyDescriptionPtr initialTopologyDescription,
    std::shared_ptr<executor::TaskExecutor> executor)
    : _sdamConfiguration(sdamConfiguration),
      _eventPublisher(eventsPublisher),
      _executor(_setupExecutor(executor)) {
    LOG(kLogDebugLevel) << "Starting Replica Set IsMaster monitor with "
                        << initialTopologyDescription->getServers().size() << " members.";
    onTopologyDescriptionChangedEvent(
        initialTopologyDescription->getId(), nullptr, initialTopologyDescription);
}

// TODO: measure if this is a bottleneck. if so, implement using
// ServerOpeningEvent/ServerClosingEvent
void ServerIsMasterMonitor::onTopologyDescriptionChangedEvent(
    UUID topologyId,
    sdam::TopologyDescriptionPtr previousDescription,
    sdam::TopologyDescriptionPtr newDescription) {
    log() << "ServerIsMasterMonitor::onTopologyDescriptionChangedEvent";
    stdx::lock_guard<Mutex> lk(_mutex);

    // remove monitors that are missing from the topology
    for (auto it = _singleMonitors.begin(); it != _singleMonitors.end(); ++it) {
        const auto& serverAddress = it->first;
        SingleServerIsMasterMonitorPtr& singleMonitor = it->second;
        if (newDescription->findServerByAddress(serverAddress) == boost::none) {
            // not in current topology -- remove the monitor
            LOG(kLogDebugLevel) << serverAddress << " was removed from the topology.";
            singleMonitor->close();
            it = _singleMonitors.erase(it);
        }
    }

    // add new monitors
    newDescription->findServers([this](const sdam::ServerDescriptionPtr& serverDescription) {
        const auto& serverAddress = serverDescription->getAddress();
        bool isMissing =
            _singleMonitors.find(serverDescription->getAddress()) == _singleMonitors.end();
        if (isMissing) {
            LOG(kLogDebugLevel) << serverAddress << " was added to the topology.";
            _singleMonitors[serverAddress] = std::make_shared<SingleServerIsMasterMonitor>(
                serverAddress,
                _sdamConfiguration.getHeartBeatFrequency(),
                _eventPublisher,
                _executor);
            _singleMonitors[serverAddress]->init();
        }
        return isMissing;
    });
}

std::shared_ptr<executor::TaskExecutor> ServerIsMasterMonitor::_setupExecutor(
    const std::shared_ptr<executor::TaskExecutor>& executor) {
    if (executor)
        return executor;

    auto hookList = std::make_unique<rpc::EgressMetadataHookList>();
    auto net = executor::makeNetworkInterface(
        "ServerIsMasterMonitor-TaskExecutor", nullptr, std::move(hookList));
    auto pool = std::make_unique<executor::NetworkInterfaceThreadPool>(net.get());
    auto result = std::make_shared<ThreadPoolTaskExecutor>(std::move(pool), std::move(net));
    result->startup();
    return result;
}
}  // namespace mongo
