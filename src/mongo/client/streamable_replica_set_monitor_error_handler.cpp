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
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork
#include "mongo/client/streamable_replica_set_monitor_error_handler.h"

#include "mongo/logv2/log.h"

namespace mongo {
const SdamErrorHandler::ErrorActions SdamErrorHandler::computeErrorActions(
    const HostAndPort& host,
    const Status& status,
    HandshakeStage handshakeStage,
    bool isApplicationOperation,
    boost::optional<BSONObj> bson) {
    // Initial state: don't drop connections, no immediate check, don't generate an error server
    // description
    ErrorActions result;

    const auto errorServerDescription = [&]() {
        result.isMasterOutcome = _createErrorIsMasterOutcome(host, bson, status);
    };
    const auto immediateCheck = [&]() { result.requestImmediateCheck = true; };
    const auto dropConnections = [&]() { result.dropConnections = true; };

    // https://github.com/mongodb/specifications/blob/master/source/server-discovery-and-monitoring/server-discovery-and-monitoring.rst#not-master-and-node-is-recovering
    if (_isNotMasterOrNotRecovering(status) && isApplicationOperation) {
        errorServerDescription();
        immediateCheck();
        if (_isNodeShuttingDown(status)) {
            dropConnections();
        }
    }

    // https://github.com/mongodb/specifications/blob/master/source/server-discovery-and-monitoring/server-discovery-and-monitoring.rst#network-error-when-reading-or-writing
    else if (_isNetworkError(status) && isApplicationOperation) {
        switch (handshakeStage) {
            case HandshakeStage::kPreHandshake:
                errorServerDescription();
                break;
            case HandshakeStage::kPostHandshake:
                if (!_isNetworkTimeout(status)) {
                    errorServerDescription();
                }
                break;
        }
        dropConnections();
    }

    // https://github.com/mongodb/specifications/blob/master/source/server-discovery-and-monitoring/server-monitoring.rst#network-error-during-server-check
    else if (_isNetworkError(status) && !isApplicationOperation) {
        dropConnections();
        errorServerDescription();
    }

    else if (!status.isOK()) {
        errorServerDescription();
    }

    LOGV2(4712102,
          "Host failed in replica set",
          "setName"_attr = _setName,
          "host"_attr = host,
          "error"_attr = status,
          "action"_attr = result);
    return result;
}

bool SdamErrorHandler::_isNodeRecovering(const Status& status) {
    return ErrorCodes::isA<ErrorCategory::NodeIsRecoveringError>(status.code());
}

bool SdamErrorHandler::_isNetworkTimeout(const Status& status) {
    return ErrorCodes::isA<ErrorCategory::NetworkTimeoutError>(status.code());
}

bool SdamErrorHandler::_isNodeShuttingDown(const Status& status) {
    return ErrorCodes::isA<ErrorCategory::ShutdownError>(status.code());
}

bool SdamErrorHandler::_isNetworkError(const Status& status) {
    return ErrorCodes::isA<ErrorCategory::NetworkError>(status.code());
}

bool SdamErrorHandler::_isNotMasterOrNotRecovering(const Status& status) {
    return _isNodeRecovering(status) || _isNotMaster(status);
}

bool SdamErrorHandler::_isNotMaster(const Status& status) {
    // There is a broader definition of "NotMaster" errors defined in error_codes.yml
    // Sticking to the strict spec interpretation for now.
    static std::set<int32_t> notMasterErrors = {ErrorCodes::NotMaster,
                                                ErrorCodes::NotMasterNoSlaveOk};
    return notMasterErrors.find(status.code()) != notMasterErrors.end();
}

BSONObj StreamableReplicaSetMonitorErrorHandler::ErrorActions::toBSON() const {
    BSONObjBuilder builder;
    builder.append("dropConnections", dropConnections);
    builder.append("requestImmediateCheck", requestImmediateCheck);
    if (isMasterOutcome) {
        builder.append("outcome", isMasterOutcome->toBSON());
    }
    return builder.obj();
}
}  // namespace mongo
