#include "mongo/client/server_is_master_monitor.h"
#include <mongo/client/sdam/sdam_datatypes.h>

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault
#include "mongo/client/sdam/sdam.h"
#include "mongo/executor/network_interface_factory.h"
#include "mongo/executor/network_interface_thread_pool.h"
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/rpc/metadata/egress_metadata_hook_list.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {
MONGO_FAIL_POINT_DEFINE(modifyReplicaSetMonitorDefaultRefreshPeriod);

const BSONObj IS_MASTER_BSON = BSON("isMaster" << 1);

using executor::NetworkInterface;
using executor::NetworkInterfaceThreadPool;
using executor::TaskExecutor;
using executor::ThreadPoolTaskExecutor;

const Milliseconds kZeroMs = Milliseconds{0};
}  // namespace

SingleServerIsMasterMonitor::SingleServerIsMasterMonitor(
    const MongoURI& setUri,
    const sdam::ServerAddress& host,
    Milliseconds heartbeatFrequencyMS,
    sdam::TopologyEventsPublisherPtr eventListener,
    std::shared_ptr<executor::TaskExecutor> executor)
    : _host(host),
      _eventListener(eventListener),
      _executor(executor),
      _heartbeatFrequencyMS(_overrideRefreshPeriod(heartbeatFrequencyMS)),
      _isClosed(true),
      _setUri(setUri) {
    LOG(kDebugLevel) << "Created Replica Set SingleServerIsMasterMonitor for host " << host;
}

void SingleServerIsMasterMonitor::init() {
    stdx::lock_guard<Mutex> lock(_mutex);
    _isClosed = false;
    _scheduleNextIsMaster(lock, Milliseconds(0));
}

void SingleServerIsMasterMonitor::requestImmediateCheck() {
    Milliseconds delayUntilNextCheck;
    stdx::lock_guard<Mutex> lock(_mutex);

    // remain in expedited mode until the replica set recovers
    if (!_isExpedited) {
        // save some log lines.
        LOG(kDebugLevel) << "[SingleServerIsMasterMonitor] Monitoring " << _host
                         << " in expedited mode until we detect a primary.";
        _isExpedited = true;
    }

    // .. but continue with rescheduling the next request.

    if (_isMasterOutstanding) {
        LOG(kDebugLevel) << "[SingleServerIsMasterMonitor] immediate isMaster check requested, but "
                            "there is already an "
                            "outstanding request.";
        return;
    }

    const Milliseconds timeSinceLastCheck =
        (_lastIsMasterAt) ? _executor->now() - *_lastIsMasterAt : Milliseconds::max();

    delayUntilNextCheck = (_lastIsMasterAt && timeSinceLastCheck < _heartbeatFrequencyMS)
        ? _heartbeatFrequencyMS - timeSinceLastCheck
        : kZeroMs;

    // if our calculated delay is less than the next scheduled call, then run the check sooner.
    // Otherwise, do nothing.
    if (delayUntilNextCheck < (_currentRefreshPeriod(lock) - timeSinceLastCheck) ||
        timeSinceLastCheck == Milliseconds::max()) {
        _cancelOutstandingRequest(lock);
    } else {
        return;
    }

    LOG(kDebugLevel) << "[SingleServerIsMasterMonitor] Rescheduling next isMaster check for "
                     << this->_host << " in " << delayUntilNextCheck;
    _scheduleNextIsMaster(lock, delayUntilNextCheck);
}

void SingleServerIsMasterMonitor::_scheduleNextIsMaster(WithLock, Milliseconds delay) {
    if (_isClosed)
        return;

    invariant(!_isMasterOutstanding);

    Timer timer;
    auto swCbHandle = _executor->scheduleWorkAt(
        _executor->now() + delay,
        [self = shared_from_this()](const executor::TaskExecutor::CallbackArgs& cbData) {
            if (!cbData.status.isOK()) {
                return;
            }
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
    request.sslMode = _setUri.getSSLMode();

    stdx::lock_guard<Mutex> lock(_mutex);
    if (_isClosed)
        return;

    Timer timer;
    auto swCbHandle = _executor->scheduleRemoteCommand(
        std::move(request),
        [self = shared_from_this(),
         timer](const executor::TaskExecutor::RemoteCommandCallbackArgs& result) mutable {
            Milliseconds nextRefreshPeriod;
            {
                stdx::lock_guard<Mutex> lk(self->_mutex);
                self->_isMasterOutstanding = false;

                if (self->_isClosed || ErrorCodes::isCancelationError(result.response.status)) {
                    LOG(kDebugLevel) << "[SingleServerIsMasterMonitor] not processing response: "
                                     << result.response.status;
                    return;
                }

                self->_lastIsMasterAt = self->_executor->now();
                nextRefreshPeriod = self->_currentRefreshPeriod(lk);
                self->_scheduleNextIsMaster(lk, nextRefreshPeriod);
            }

            Microseconds latency(timer.micros());
            if (result.response.isOK()) {
                self->_onIsMasterSuccess(latency, result.response.data);
            } else {
                self->_onIsMasterFailure(latency, result.response.status, result.response.data);
            }
        });

    if (!swCbHandle.isOK()) {
        mongo::Microseconds latency(timer.micros());
        _onIsMasterFailure(latency, swCbHandle.getStatus(), BSONObj());
        fassertFailedWithStatus(31448, swCbHandle.getStatus());
    }

    _isMasterOutstanding = true;
    _remoteCommandHandle = swCbHandle.getValue();
}

void SingleServerIsMasterMonitor::close() {
    stdx::lock_guard<Mutex> lock(_mutex);
    LOG(kDebugLevel) << "Closing Replica Set SingleServerIsMasterMonitor for host " << _host;
    _isClosed = true;

    _cancelOutstandingRequest(lock);

    _executor = nullptr;
    LOG(kDebugLevel) << "Done Closing Replica Set SingleServerIsMasterMonitor for host " << _host;
}

void SingleServerIsMasterMonitor::_cancelOutstandingRequest(WithLock) {
    if (_nextIsMasterHandle.isValid()) {
        _executor->cancel(_nextIsMasterHandle);
    }

    if (_remoteCommandHandle.isValid()) {
        _executor->cancel(_remoteCommandHandle);
    }

    _isMasterOutstanding = false;
}

void SingleServerIsMasterMonitor::_onIsMasterSuccess(sdam::IsMasterRTT latency,
                                                     const BSONObj bson) {
    _eventListener->onServerHeartbeatSucceededEvent(
        duration_cast<Milliseconds>(latency), _host, bson);
}

void SingleServerIsMasterMonitor::_onIsMasterFailure(sdam::IsMasterRTT latency,
                                                     const Status& status,
                                                     const BSONObj bson) {
    LOG(kDebugLevel) << "received failed isMaster for server " << _host << ": " << status.toString()
                     << " (" << latency << ")"
                     << "; " << bson.toString();
    _eventListener->onServerHeartbeatFailureEvent(
        duration_cast<Milliseconds>(latency), status, _host, bson);
}

Milliseconds SingleServerIsMasterMonitor::_overrideRefreshPeriod(Milliseconds original) {
    Milliseconds r = original;
    static constexpr auto kPeriodField = "period"_sd;
    modifyReplicaSetMonitorDefaultRefreshPeriod.executeIf(
        [&r](const BSONObj& data) {
            r = duration_cast<Milliseconds>(Seconds{data.getIntField(kPeriodField)});
        },
        [](const BSONObj& data) { return data.hasField(kPeriodField); });
    return r;
}

Milliseconds SingleServerIsMasterMonitor::_currentRefreshPeriod(WithLock) {
    return (_isExpedited) ? sdam::SdamConfiguration::kMinHeartbeatFrequencyMS
                          : _heartbeatFrequencyMS;
}

void SingleServerIsMasterMonitor::disableExpeditedChecking() {
    stdx::lock_guard<Mutex> lock(_mutex);
    _isExpedited = false;
}


ServerIsMasterMonitor::ServerIsMasterMonitor(
    const MongoURI& setUri,
    const sdam::SdamConfiguration& sdamConfiguration,
    sdam::TopologyEventsPublisherPtr eventsPublisher,
    sdam::TopologyDescriptionPtr initialTopologyDescription,
    std::shared_ptr<executor::TaskExecutor> executor)
    : _sdamConfiguration(sdamConfiguration),
      _eventPublisher(eventsPublisher),
      _executor(_setupExecutor(executor)),
      _isClosed(false),
      _setUri(setUri) {
    LOG(kLogDebugLevel) << "Starting Replica Set IsMaster monitor with "
                        << initialTopologyDescription->getServers().size() << " members.";
    onTopologyDescriptionChangedEvent(
        initialTopologyDescription->getId(), nullptr, initialTopologyDescription);
}

void ServerIsMasterMonitor::close() {
    stdx::lock_guard<Mutex> lock(_mutex);
    if (_isClosed)
        return;

    _isClosed = true;
    for (auto singleMonitor : _singleMonitors) {
        singleMonitor.second->close();
    }
}

void ServerIsMasterMonitor::onTopologyDescriptionChangedEvent(
    UUID topologyId,
    sdam::TopologyDescriptionPtr previousDescription,
    sdam::TopologyDescriptionPtr newDescription) {
    stdx::lock_guard<Mutex> lock(_mutex);
    if (_isClosed)
        return;

    const auto newType = newDescription->getType();
    using sdam::TopologyType;

    if (newType == TopologyType::kSingle || newType == TopologyType::kReplicaSetWithPrimary ||
        newType == TopologyType::kSharded) {
        _disableExpeditedChecking(lock);
    }

    // remove monitors that are missing from the topology
    auto it = _singleMonitors.begin();
    while (it != _singleMonitors.end()) {
        const auto& serverAddress = it->first;
        if (newDescription->findServerByAddress(serverAddress) == boost::none) {
            auto& singleMonitor = _singleMonitors[serverAddress];
            singleMonitor->close();
            LOG(kLogDebugLevel) << serverAddress << " was removed from the topology.";
            it = _singleMonitors.erase(it);
        } else {
            ++it;
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
                _setUri,
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

void ServerIsMasterMonitor::requestImmediateCheck() {
    stdx::lock_guard<Mutex> lock(_mutex);
    for (auto& addressAndMonitor : _singleMonitors) {
        addressAndMonitor.second->requestImmediateCheck();
    }
}

void ServerIsMasterMonitor::_disableExpeditedChecking(WithLock) {
    for (auto& addressAndMonitor : _singleMonitors) {
        addressAndMonitor.second->disableExpeditedChecking();
    }
}
}  // namespace mongo
