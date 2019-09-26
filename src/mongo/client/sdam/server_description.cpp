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

#include "mongo/client/sdam/server_description.h"
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <set>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/oid.h"
#include "mongo/client/sdam/sdam_datatypes.h"
#include "mongo/util/duration.h"
#include "mongo/util/log.h"


namespace mongo::sdam {
const ServerAddress& ServerDescription::getAddress() const {
    return _address;
}

const boost::optional<std::string>& ServerDescription::getError() const {
    return _error;
}

const boost::optional<IsMasterLatency>& ServerDescription::getRtt() const {
    return _rtt;
}

const boost::optional<mongo::Date_t>& ServerDescription::getLastWriteDate() const {
    return _lastWriteDate;
}

const boost::optional<repl::OpTime>& ServerDescription::getOpTime() const {
    return _opTime;
}

ServerType ServerDescription::getType() const {
    return _type;
}

const boost::optional<ServerAddress>& ServerDescription::getMe() const {
    return _me;
}

const std::set<ServerAddress>& ServerDescription::getHosts() const {
    return _hosts;
}

const std::set<ServerAddress>& ServerDescription::getPassives() const {
    return _passives;
}

const std::set<ServerAddress>& ServerDescription::getArbiters() const {
    return _arbiters;
}

const std::map<std::string, std::string>& ServerDescription::getTags() const {
    return _tags;
}

const boost::optional<std::string>& ServerDescription::getSetName() const {
    return _setName;
}

const boost::optional<int>& ServerDescription::getSetVersion() const {
    return _setVersion;
}

const boost::optional<mongo::OID>& ServerDescription::getElectionId() const {
    return _electionId;
}

const boost::optional<ServerAddress>& ServerDescription::getPrimary() const {
    return _primary;
}

const mongo::Date_t ServerDescription::getLastUpdateTime() const {
    return *_lastUpdateTime;
}

const boost::optional<int>& ServerDescription::getLogicalSessionTimeoutMinutes() const {
    return _logicalSessionTimeoutMinutes;
}

bool ServerDescription::isEquivalent(const ServerDescription& other) const {
    auto typeEqual = _type == other._type;
    auto minWireVersionEqual = _minWireVersion == other._minWireVersion;
    auto maxWireVersionEqual = _maxWireVersion == other._maxWireVersion;
    auto meEqual = _me == other._me;
    auto hostsEqual = _hosts == other._hosts;
    auto passivesEqual = _passives == other._passives;
    auto arbitersEqual = _arbiters == other._arbiters;
    auto tagsEqual = _tags == other._tags;
    auto namesEqual = _setName == other._setName;
    auto versionsEqual = _setVersion == other._setVersion;
    auto electionIdEqual = _electionId == other._electionId;
    auto primaryEqual = _primary == other._primary;
    auto lsTimeoutEqual = _logicalSessionTimeoutMinutes == other._logicalSessionTimeoutMinutes;
    return typeEqual && minWireVersionEqual && maxWireVersionEqual && meEqual && hostsEqual &&
        passivesEqual && arbitersEqual && tagsEqual && namesEqual && versionsEqual &&
        electionIdEqual && primaryEqual && lsTimeoutEqual;
}

bool ServerDescription::isDataBearingServer() const {
    return DATA_SERVER_TYPES.find(_type) != DATA_SERVER_TYPES.end();
}

// output server description to bson. This is primarily used for debugging.
BSONObj ServerDescription::toBson() const {
    BSONObjBuilder bson;
    bson.append("address", _address);
    if (_rtt) {
        bson.append("roundTripTime", durationCount<Microseconds>(*_rtt));
    } else {
        bson.appendNull("roundTripTime");
    }
    if (_lastWriteDate) {
        bson.appendDate("lastWriteDate", *_lastWriteDate);
    } else {
        bson.appendNull("lastWriteDate");
    }
    if (_opTime) {
        bson.append("opTime", _opTime->toBSON());
    } else {
        bson.appendNull("opTime");
    }
    bson.append("type", _type);
    bson.append("minWireVersion", _minWireVersion);
    bson.append("maxWireVersion", _maxWireVersion);
    if (_me) {
        bson.append("me", *_me);
    } else {
        bson.appendNull("me");
    }
    if (_setName) {
        bson.append("setName", *_setName);
    } else {
        bson.appendNull("setName");
    }
    if (_setVersion) {
        bson.append("setVersion", *_setVersion);
    } else {
        bson.appendNull("setVersion");
    }
    if (_electionId) {
        bson.append("electionId", *_electionId);
    } else {
        bson.appendNull("electionId");
    }
    if (_primary) {
        bson.append("primary", *_primary);
    } else {
        bson.appendNull("primary");
    }
    if (_lastUpdateTime) {
        bson.append("lastUpdateTime", *_lastUpdateTime);
    } else {
        bson.append("lastUpdateTime", Date_t::min());
    }
    if (_logicalSessionTimeoutMinutes) {
        bson.append("logicalSessionTimeoutMinutes", *_logicalSessionTimeoutMinutes);
    } else {
        bson.appendNull("logicalSessionTimeoutMinutes");
    }
    return bson.obj();
}

int ServerDescription::getMinWireVersion() const {
    return _minWireVersion;
}

int ServerDescription::getMaxWireVersion() const {
    return _maxWireVersion;
}

ServerDescriptionBuilder::ServerDescriptionBuilder(ClockSource* clockSource,
                                                   const IsMasterOutcome& isMasterOutcome,
                                                   boost::optional<IsMasterLatency> lastRtt) {
    withAddress(boost::to_lower_copy(isMasterOutcome.getServer()));
    if (isMasterOutcome.isSuccess()) {
        const auto response = *isMasterOutcome.getResponse();
        parseTypeFromIsMaster(response);

        calculateRtt(*isMasterOutcome.getRtt(), lastRtt);

        withLastUpdateTime(clockSource->now());
        withMinWireVersion(response["minWireVersion"].numberInt());
        withMaxWireVersion(response["maxWireVersion"].numberInt());

        saveLastWriteInfo(response.getObjectField("lastWrite"));
        saveHosts(response);
        saveTags(response.getObjectField("tags"));
        saveElectionId(response.getField("electionId"));

        auto lsTimeoutField = response.getField("logicalSessionTimeoutMinutes");
        if (lsTimeoutField.type() == BSONType::NumberInt) {
            withLogicalSessionTimeoutMinutes(lsTimeoutField.numberInt());
        }

        auto setVersionField = response.getField("setVersion");
        if (setVersionField.type() == BSONType::NumberInt) {
            withSetVersion(response["setVersion"].numberInt());
        }

        auto setNameField = response.getField("setName");
        if (setNameField.type() == BSONType::String) {
            withSetName(response["setName"].str());
        }

        auto primaryField = response.getField("primary");
        if (primaryField.type() == BSONType::String) {
            withPrimary(response.getStringField("primary"));
        }
    } else {
        withError(isMasterOutcome.getErrorMsg());
    }
}

void ServerDescriptionBuilder::saveElectionId(BSONElement electionId) {
    if (electionId.type() == jstOID) {
        withElectionId(electionId.OID());
    }
}

void ServerDescriptionBuilder::calculateRtt(const IsMasterLatency currentRtt,
                                            const boost::optional<IsMasterLatency> lastRtt) {
    if (_instance.getType() != ServerType::kUnknown) {
        if (lastRtt) {
            withRtt(currentRtt, *lastRtt);
        } else {
            withRtt(currentRtt);
        }
    }
}

void ServerDescriptionBuilder::saveLastWriteInfo(BSONObj lastWriteBson) {
    const auto lastWriteDateField = lastWriteBson.getField("lastWriteDate");
    if (lastWriteDateField.type() == BSONType::Date) {
        withLastWriteDate(lastWriteDateField.date());
    }

    const auto opTimeParse =
        repl::OpTime::parseFromOplogEntry(lastWriteBson.getObjectField("opTime"));
    if (opTimeParse.isOK()) {
        withOpTime(opTimeParse.getValue());
    }
}

void ServerDescriptionBuilder::parseTypeFromIsMaster(const BSONObj isMaster) {
    ServerType t;
    bool hasSetName = isMaster.hasField("setName");

    if (isMaster.getField("ok").numberInt() != 1) {
        t = ServerType::kUnknown;
    } else if (!hasSetName && !isMaster.hasField("msg") && !isMaster.getBoolField("isreplicaset")) {
        t = ServerType::kStandalone;
    } else if (IS_DB_GRID == isMaster.getStringField("msg")) {
        t = ServerType::kMongos;
    } else if (hasSetName && isMaster.getBoolField("ismaster")) {
        t = ServerType::kRSPrimary;
    } else if (hasSetName && isMaster.getBoolField("secondary")) {
        t = ServerType::kRSSecondary;
    } else if (hasSetName && isMaster.getBoolField("arbiterOnly")) {
        t = ServerType::kRSArbiter;
    } else if (hasSetName && isMaster.getBoolField("hidden")) {
        t = ServerType::kRSOther;
    } else if (isMaster.getBoolField("isreplicaset")) {
        t = ServerType::kRSGhost;
    } else {
        // TODO: check for appropriate log level
        MONGO_LOG(0) << "unknown server type from successful ismaster reply: "
                     << isMaster.toString();
        t = ServerType::kUnknown;
    }
    withType(t);
}

ServerDescription ServerDescriptionBuilder::instance() const {
    return std::move(_instance);
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withAddress(const ServerAddress& address) {
    _instance._address = address;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withError(const std::string& error) {
    _instance._error = error;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withRtt(
    const IsMasterLatency& rtt, boost::optional<IsMasterLatency> lastRtt) {
    if (lastRtt) {
        // new_rtt = alpha * x + (1 - alpha) * old_rtt
        _instance._rtt = IsMasterLatency(static_cast<IsMasterLatency::rep>(
            RTT_ALPHA * rtt.count() + (1 - RTT_ALPHA) * lastRtt.get().count()));
    } else {
        _instance._rtt = rtt;
    }
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withLastWriteDate(const Date_t& lastWriteDate) {
    _instance._lastWriteDate = lastWriteDate;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withOpTime(const repl::OpTime opTime) {
    _instance._opTime = opTime;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withType(const ServerType type) {
    _instance._type = type;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withMinWireVersion(int minVersion) {
    _instance._minWireVersion = minVersion;
    return *this;
}
ServerDescriptionBuilder& ServerDescriptionBuilder::withMaxWireVersion(int maxVersion) {
    _instance._maxWireVersion = maxVersion;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withMe(const ServerAddress& me) {
    _instance._me = boost::to_lower_copy(me);
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withHost(const ServerAddress& host) {
    _instance._hosts.emplace(boost::to_lower_copy(host));
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withPassive(const ServerAddress& passive) {
    _instance._passives.emplace(boost::to_lower_copy(passive));
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withArbiter(const ServerAddress& arbiter) {
    _instance._arbiters.emplace(boost::to_lower_copy(arbiter));
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withTag(const std::string key,
                                                            const std::string value) {
    _instance._tags[key] = value;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withSetName(const std::string setName) {
    _instance._setName = std::move(setName);
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withSetVersion(const int setVersion) {
    _instance._setVersion = setVersion;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withElectionId(const OID& electionId) {
    _instance._electionId = electionId;
    return *this;
}
ServerDescriptionBuilder& ServerDescriptionBuilder::withPrimary(const ServerAddress& primary) {
    _instance._primary = primary;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withLastUpdateTime(
    const Date_t& lastUpdateTime) {
    _instance._lastUpdateTime = lastUpdateTime;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withLogicalSessionTimeoutMinutes(
    const int logicalSessionTimeoutMinutes) {
    _instance._logicalSessionTimeoutMinutes = logicalSessionTimeoutMinutes;
    return *this;
}

void ServerDescriptionBuilder::storeHostListIfPresent(const std::string key,
                                                      const BSONObj response,
                                                      std::set<ServerAddress>& destination) {
    if (response.hasField(key)) {
        auto hostsBsonArray = response[key].Array();
        std::transform(hostsBsonArray.begin(),
                       hostsBsonArray.end(),
                       std::inserter(destination, destination.begin()),
                       [](const BSONElement e) { return boost::to_lower_copy(e.String()); });
    }
}

void ServerDescriptionBuilder::saveHosts(const BSONObj response) {
    if (response.hasField("me")) {
        withMe(response.getStringField("me"));
    }

    storeHostListIfPresent("hosts", response, _instance._hosts);
    storeHostListIfPresent("passives", response, _instance._passives);
    storeHostListIfPresent("arbiters", response, _instance._arbiters);
}

void ServerDescriptionBuilder::saveTags(BSONObj tagsObj) {
    const auto keys = tagsObj.getFieldNames<std::set<std::string>>();
    for (const auto key : keys) {
        withTag(key, tagsObj.getStringField(key));
    }
}

bool operator==(const mongo::sdam::ServerDescription& a, const mongo::sdam::ServerDescription& b) {
    return a.isEquivalent(b);
}

bool operator!=(const mongo::sdam::ServerDescription& a, const mongo::sdam::ServerDescription& b) {
    return !(a == b);
}

std::ostream& operator<<(std::ostream& os, const ServerDescription& description) {
    BSONObj obj = description.toBson();
    os << obj.toString();
    return os;
}
};  // namespace mongo::sdam
