/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include "mongo/client/sdam/sdam_datatypes.h"

namespace mongo::sdam {
std::string toString(ServerType serverType) {
    switch (serverType) {
        case ServerType::kStandalone:
            return "Standalone";
        case ServerType::kMongos:
            return "Mongos";
        case ServerType::kRSPrimary:
            return "RSPrimary";
        case ServerType::kRSSecondary:
            return "RSSecondary";
        case ServerType::kRSArbiter:
            return "RSArbiter";
        case ServerType::kRSOther:
            return "RSOther";
        case ServerType::kRSGhost:
            return "RSGhost";
        case ServerType::kUnknown:
            return "Unknown";
        default:
            MONGO_UNREACHABLE;
    }
}

const std::vector<ServerType> allServerTypes() {
    static auto const result = std::vector<ServerType>{ServerType::kStandalone,
                                                       ServerType::kMongos,
                                                       ServerType::kRSPrimary,
                                                       ServerType::kRSSecondary,
                                                       ServerType::kRSArbiter,
                                                       ServerType::kRSOther,
                                                       ServerType::kRSGhost,
                                                       ServerType::kUnknown};
    return result;
}

const std::vector<TopologyType> allTopologyTypes() {
    static auto const result = std::vector<TopologyType>{TopologyType::kSingle,
                                                         TopologyType::kReplicaSetNoPrimary,
                                                         TopologyType::kReplicaSetWithPrimary,
                                                         TopologyType::kSharded,
                                                         TopologyType::kUnknown};
    return result;
}

std::string toString(TopologyType topologyType) {
    switch (topologyType) {
        case TopologyType::kReplicaSetNoPrimary:
            return "ReplicaSetNoPrimary";
        case TopologyType::kReplicaSetWithPrimary:
            return "ReplicaSetWithPrimary";
        case TopologyType::kSharded:
            return "Sharded";
        case TopologyType::kUnknown:
            return "Unknown";
        case TopologyType::kSingle:
            return "Single";
        default:
            MONGO_UNREACHABLE
    }
}


const ServerAddress& IsMasterOutcome::getServer() const {
    return _server;
}
bool IsMasterOutcome::isSuccess() const {
    return _success;
}
const boost::optional<BSONObj>& IsMasterOutcome::getResponse() const {
    return _response;
}
const boost::optional<IsMasterLatency>& IsMasterOutcome::getRtt() const {
    return _rtt;
}
const std::string& IsMasterOutcome::getErrorMsg() const {
    return _errorMsg;
}
};  // namespace mongo::sdam
