#include "mongo/client/sdam/server_description.h"

#include <set>

#include "boost/optional.hpp"

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/oid.h"
#include "mongo/client/sdam/datatypes.h"
#include "mongo/util/duration.h"
#include "mongo/util/log.h"


namespace mongo::sdam {
const ServerAddress& ServerDescription::getAddress() const {
    return _address;
}

const boost::optional<std::string>& ServerDescription::getError() const {
    return _error;
}

const boost::optional<OpLatency>& ServerDescription::getRtt() const {
    return _rtt;
}

const boost::optional<mongo::Date_t>& ServerDescription::getLastWriteDate() const {
    return _lastWriteDate;
}

const boost::optional<mongo::OID>& ServerDescription::getOpTime() const {
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

const boost::optional<mongo::Date_t>& ServerDescription::getLastUpdateTime() const {
    return _lastUpdateTime;
}

const boost::optional<int>& ServerDescription::getLogicalSessionTimeoutMinutes() const {
    return _logicalSessionTimeoutMinutes;
}

bool ServerDescription::operator==(const ServerDescription& other) const {
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

bool ServerDescription::operator!=(const mongo::sdam::ServerDescription& other) const {
    return !(*this == other);
}

static const std::set<ServerType> dataServerTypes{
    ServerType::Mongos, ServerType::RSPrimary, ServerType::RSSecondary, ServerType::Standalone};
bool ServerDescription::isDataBearingServer() const {
    return dataServerTypes.find(_type) != dataServerTypes.end();
}

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
        bson.append("opTime", *_opTime);
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
    // TODO: hosts,passives,arbiters,tags
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


ServerDescriptionBuilder::ServerDescriptionBuilder(const IsMasterOutcome& isMasterOutcome) {
    if (isMasterOutcome.isSuccess()) {
        parseTypeFromIsMaster(*isMasterOutcome.getResponse());
    } else {
        withError(isMasterOutcome.getErrorMsg());
    }
}

void ServerDescriptionBuilder::parseTypeFromIsMaster(const BSONObj isMaster) {
    ServerType t;
    bool hasSetName = isMaster.hasField("setName");

    if (isMaster.getField("ok").numberInt() != 1) {
        t = ServerType::Unknown;
    } else if (!hasSetName && !isMaster.hasField("msg") && !isMaster.getBoolField("isreplicaset")) {
        t = ServerType::Standalone;
    } else if (IS_DB_GRID == isMaster.getStringField("msg")) {
        t = ServerType::Mongos;
    } else if (hasSetName && isMaster.getBoolField("ismaster")) {
        t = ServerType::RSPrimary;
    } else if (hasSetName && isMaster.getBoolField("secondary")) {
        t = ServerType::RSSecondary;
    } else if (hasSetName && isMaster.getBoolField("arbiterOnly")) {
        t = ServerType::RSArbiter;
    } else if (hasSetName && isMaster.getBoolField("hidden")) {
        t = ServerType::RSOther;
    } else if (isMaster.getBoolField("isreplicaset")) {
        t = ServerType::RSGhost;
    } else {
        // TODO: what are the log levels?
        MONGO_LOG(3) << "unknown server type from successful ismaster reply: "
                     << isMaster.toString();
        t = ServerType::Unknown;
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

ServerDescriptionBuilder& ServerDescriptionBuilder::withRtt(const OpLatency& rtt) {
    _instance._rtt = rtt;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withLastWriteDate(const Date_t& lastWriteDate) {
    _instance._lastWriteDate = lastWriteDate;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withOpTime(const OID& opTime) {
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
    _instance._me = me;
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withHost(const ServerAddress& host) {
    _instance._hosts.emplace(host);
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withPassive(const ServerAddress& passive) {
    _instance._passives.emplace(passive);
    return *this;
}

ServerDescriptionBuilder& ServerDescriptionBuilder::withArbiter(const ServerAddress& arbiter) {
    _instance._arbiters.emplace(arbiter);
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
};  // namespace mongo::sdam
