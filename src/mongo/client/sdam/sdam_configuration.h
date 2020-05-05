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
#pragma once

#include "mongo/client/sdam/sdam_datatypes.h"

#include "mongo/client/sdam/sdam_configuration_parameters_gen.h"

namespace mongo::sdam {
class SdamConfiguration {
public:
    SdamConfiguration() : SdamConfiguration(boost::none){};

    /**
     * Initialize the TopologyDescription. This constructor may uassert if the provided
     * configuration options are not valid according to the Server Discovery & Monitoring Spec.
     *
     * Initial Servers
     * initial servers may be set to a seed list of one or more server addresses.
     *
     * Initial TopologyType
     * The initial TopologyType may be set to Single, Unknown, or ReplicaSetNoPrimary.
     *
     * Initial setName
     * The client's initial replica set name is required in order to initially configure the
     * topology type as ReplicaSetNoPrimary.
     *
     * Allowed configuration combinations
     * TopologyType Single cannot be used with multiple seeds.
     * If setName is not null, only TopologyType ReplicaSetNoPrimary and Single, are
     * allowed.
     */
    explicit SdamConfiguration(
        boost::optional<std::vector<ServerAddress>> seedList,
        TopologyType initialType = TopologyType::kUnknown,
        Milliseconds heartBeatFrequencyMs = Milliseconds(sdamHeartBeatFrequencyMs),
        Milliseconds connectTimeoutMs = Milliseconds(sdamConnectTimeoutMs),
        Milliseconds localThreshholdMs = Milliseconds(sdamLocalThreshholdMs),
        Milliseconds serverSelectionTimeoutMs = Milliseconds(sdamServerSelectionTimeoutMs),
        boost::optional<std::string> setName = boost::none);

    const boost::optional<std::vector<ServerAddress>>& getSeedList() const;
    TopologyType getInitialType() const;
    const boost::optional<std::string>& getSetName() const;

    Milliseconds getHeartBeatFrequency() const;
    Milliseconds getConnectionTimeout() const;
    Milliseconds getLocalThreshold() const;
    Milliseconds getServerSelectionTimeout() const;

    const BSONObj& toBson() const {
        return _bsonDoc;
    }

    static constexpr Milliseconds kMinHeartbeatFrequencyMS = Milliseconds(500);

private:
    BSONObj _toBson() const;

    boost::optional<std::vector<ServerAddress>> _seedList;
    TopologyType _initialType;
    Milliseconds _heartbeatFrequency;
    Milliseconds _connectionTimeout;
    Milliseconds _localThreshold;
    Milliseconds _serverSelectionTimeout;
    boost::optional<std::string> _setName;
    BSONObj _bsonDoc;
};
}  // namespace mongo::sdam
