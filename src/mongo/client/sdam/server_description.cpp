#include "mongo/client/sdam/server_description.h"

#include <set>

#include "boost/optional.hpp"

#include "mongo/bson/oid.h"
#include "mongo/client/sdam/datatypes.h"
#include "mongo/util/duration.h"

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
const std::vector<ServerAddress>& ServerDescription::getHosts() const {
    return _hosts;
}
const std::vector<ServerAddress>& ServerDescription::getPassives() const {
    return _passives;
}
const std::vector<ServerAddress>& ServerDescription::getArbiters() const {
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
    auto wireVersionsEqual = true;  // TODO
    auto meEqual = _me == other._me;
    auto tagsEqual = _tags == other._tags;
    auto namesEqual = _setName == other._setName;
    auto versionsEqual = _setVersion == other._setVersion;
    auto electionIdEqual = _electionId == other._electionId;
    auto primaryEqual = _primary == other._primary;
    auto lsTimeoutEqual = _logicalSessionTimeoutMinutes == other._logicalSessionTimeoutMinutes;
    return typeEqual && wireVersionsEqual && meEqual && tagsEqual && namesEqual && versionsEqual &&
        electionIdEqual && primaryEqual && lsTimeoutEqual;
}
bool ServerDescription::isDataBearingServer() const {
    static std::set<ServerType> dataServerTypes{
        ServerType::Mongos, ServerType::RSPrimary, ServerType::RSSecondary, ServerType::Standalone};
    return dataServerTypes.find(_type) != dataServerTypes.end();
}
};  // namespace mongo::sdam
