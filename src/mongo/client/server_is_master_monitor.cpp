/**
 *    Copyright (C) 2020-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */
#include "mongo/client/server_is_master_monitor.h"

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault
#include "mongo/client/replica_set_monitor.h"
#include "mongo/client/sdam/sdam.h"
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
      _isShutdown(true),
      _setUri(setUri) {
    LOG(kLogLevel.lessSevere()) << "Created Replica Set SingleServerIsMasterMonitor for host "
                                << host;
}

void SingleServerIsMasterMonitor::init() {
    stdx::lock_guard lock(_mutex);
    _isShutdown = false;
    _scheduleNextIsMaster(lock, Milliseconds(0));
}

void SingleServerIsMasterMonitor::requestImmediateCheck() {
    Milliseconds delayUntilNextCheck;
    stdx::lock_guard lock(_mutex);
    if (_isShutdown)
        return;

    // remain in expedited mode until the replica set recovers
    if (!_isExpedited) {
        // save some log lines.
        LOG(kLogLevel) << "[SingleServerIsMasterMonitor] Monitoring " << _host
                       << " in expedited mode until we detect a primary.";
        _isExpedited = true;
    }

    // .. but continue with rescheduling the next request.

    if (_isMasterOutstanding) {
        LOG(kLogLevel) << "[SingleServerIsMasterMonitor] immediate isMaster check requested, but "
                          "there is already an "
                          "outstanding request.";
        return;
    }

    const auto currentRefreshPeriod = _currentRefreshPeriod(lock);

    const Milliseconds timeSinceLastCheck =
        (_lastIsMasterAt) ? _executor->now() - *_lastIsMasterAt : Milliseconds::max();

    delayUntilNextCheck = (_lastIsMasterAt && (timeSinceLastCheck < currentRefreshPeriod))
        ? currentRefreshPeriod - timeSinceLastCheck
        : kZeroMs;

    // if our calculated delay is less than the next scheduled call, then run the check sooner.
    // Otherwise, do nothing. Three cases to cancel existing request:
    // 1. refresh period has changed to expedited, so (currentRefreshPeriod - timeSinceLastCheck) is
    // < 0
    // 2. calculated delay is less then next scheduled isMaster
    // 3. isMaster was never scheduled.
    if (((currentRefreshPeriod - timeSinceLastCheck) < kZeroMs) ||
        (delayUntilNextCheck < (currentRefreshPeriod - timeSinceLastCheck)) ||
        timeSinceLastCheck == Milliseconds::max()) {
        _cancelOutstandingRequest(lock);
    } else {
        return;
    }

    LOG(kLogLevel) << "[SingleServerIsMasterMonitor] Rescheduling next isMaster check for "
                   << this->_host << " in " << delayUntilNextCheck;
    _scheduleNextIsMaster(lock, delayUntilNextCheck);
}

void SingleServerIsMasterMonitor::_scheduleNextIsMaster(WithLock, Milliseconds delay) {
    if (_isShutdown)
        return;

    invariant(!_isMasterOutstanding);

    Timer timer;
    auto swCbHandle = _executor->scheduleWorkAt(
        _executor->now() + delay,
        [self = shared_from_this()](const executor::TaskExecutor::CallbackArgs& cbData) noexcept {
            try {
                uassertStatusOK(cbData.status);
                self->_doRemoteCommand();
            } catch (ExceptionForCat<ErrorCategory::ShutdownError>&) {
            }
        });

    if (!swCbHandle.isOK()) {
        Microseconds latency(timer.micros());
        _onIsMasterFailure(latency, swCbHandle.getStatus(), BSONObj());
        return;
    }

    _nextIsMasterHandle = swCbHandle.getValue();
}

void SingleServerIsMasterMonitor::_doRemoteCommand() {
    auto request = executor::RemoteCommandRequest(
        HostAndPort(_host), "admin", IS_MASTER_BSON, nullptr, _timeoutMS);
    request.sslMode = _setUri.getSSLMode();

    stdx::lock_guard lock(_mutex);
    if (_isShutdown)
        return;

    Timer timer;
    auto swCbHandle = _executor->scheduleRemoteCommand(
        std::move(request),
        [self = shared_from_this(),
         timer](const executor::TaskExecutor::RemoteCommandCallbackArgs& result) mutable {
            Milliseconds nextRefreshPeriod;
            {
                stdx::lock_guard lk(self->_mutex);
                self->_isMasterOutstanding = false;

                if (self->_isShutdown || ErrorCodes::isCancelationError(result.response.status)) {
                    LOG(kLogLevel) << "[SingleServerIsMasterMonitor] not processing response: "
                                   << result.response.status;
                    return;
                }

                self->_lastIsMasterAt = self->_executor->now();
                nextRefreshPeriod = self->_currentRefreshPeriod(lk);
                LOG(kLogLevel.lessSevere())
                    << "next refresh period in " + nextRefreshPeriod.toString();
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
        Microseconds latency(timer.micros());
        _onIsMasterFailure(latency, swCbHandle.getStatus(), BSONObj());
        uassertStatusOK(swCbHandle);
    }

    _isMasterOutstanding = true;
    _remoteCommandHandle = swCbHandle.getValue();
}

void SingleServerIsMasterMonitor::shutdown() {
    stdx::lock_guard lock(_mutex);
    if (std::exchange(_isShutdown, true))
        return;

    LOG(kLogLevel.lessSevere()) << "Closing Replica Set SingleServerIsMasterMonitor for host "
                                << _host;

    _cancelOutstandingRequest(lock);

    _executor = nullptr;
    LOG(kLogLevel.lessSevere()) << "Done Closing Replica Set SingleServerIsMasterMonitor for host "
                                << _host;
}

void SingleServerIsMasterMonitor::_cancelOutstandingRequest(WithLock) {
    if (_nextIsMasterHandle) {
        _executor->cancel(_nextIsMasterHandle);
    }

    if (_remoteCommandHandle) {
        _executor->cancel(_remoteCommandHandle);
    }

    _isMasterOutstanding = false;
}

void SingleServerIsMasterMonitor::_onIsMasterSuccess(sdam::IsMasterRTT latency,
                                                     const BSONObj bson) {
    LOG(kLogLevel.lessSevere()) << "received successful isMaster for server " << _host << " ("
                                << latency << ")"
                                << "; " << bson.toString();
    _eventListener->onServerHeartbeatSucceededEvent(
        duration_cast<Milliseconds>(latency), _host, bson);
}

void SingleServerIsMasterMonitor::_onIsMasterFailure(sdam::IsMasterRTT latency,
                                                     const Status& status,
                                                     const BSONObj bson) {
    LOG(kLogLevel) << "received failed isMaster for server " << _host << ": " << status.toString()
                   << " (" << latency << ")"
                   << "; " << bson.toString();
    _eventListener->onServerHeartbeatFailureEvent(
        duration_cast<Milliseconds>(latency), status, _host, bson);
}

Milliseconds SingleServerIsMasterMonitor::_overrideRefreshPeriod(Milliseconds original) {
    Milliseconds r = original;
    static constexpr auto kPeriodField = "period"_sd;
    if (auto modifyReplicaSetMonitorDefaultRefreshPeriod =
            globalFailPointRegistry().find("modifyReplicaSetMonitorDefaultRefreshPeriod")) {
        modifyReplicaSetMonitorDefaultRefreshPeriod->executeIf(
            [&r](const BSONObj& data) {
                r = duration_cast<Milliseconds>(Seconds{data.getIntField(kPeriodField)});
            },
            [](const BSONObj& data) { return data.hasField(kPeriodField); });
    }
    return r;
}

Milliseconds SingleServerIsMasterMonitor::_currentRefreshPeriod(WithLock) {
    return (_isExpedited) ? sdam::SdamConfiguration::kMinHeartbeatFrequencyMS
                          : _heartbeatFrequencyMS;
}

void SingleServerIsMasterMonitor::disableExpeditedChecking() {
    stdx::lock_guard lock(_mutex);
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
      _isShutdown(false),
      _setUri(setUri) {
    LOG(kLogLevel) << "Starting Replica Set IsMaster monitor with "
                   << initialTopologyDescription->getServers().size() << " members.";
    onTopologyDescriptionChangedEvent(
        initialTopologyDescription->getId(), nullptr, initialTopologyDescription);
}

void ServerIsMasterMonitor::shutdown() {
    stdx::lock_guard lock(_mutex);
    if (_isShutdown)
        return;

    _isShutdown = true;
    for (auto singleMonitor : _singleMonitors) {
        singleMonitor.second->shutdown();
    }
}

void ServerIsMasterMonitor::onTopologyDescriptionChangedEvent(
    UUID topologyId,
    sdam::TopologyDescriptionPtr previousDescription,
    sdam::TopologyDescriptionPtr newDescription) {
    stdx::lock_guard lock(_mutex);
    if (_isShutdown)
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
            singleMonitor->shutdown();
            LOG(kLogLevel) << serverAddress << " was removed from the topology.";
            it = _singleMonitors.erase(it, ++it);
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
            LOG(kLogLevel) << serverAddress << " was added to the topology.";
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
    stdx::lock_guard lock(_mutex);
    if (_isShutdown)
        return;

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
