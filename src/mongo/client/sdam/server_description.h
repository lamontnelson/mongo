#pragma once
#include <map>
#include <set>
#include <utility>

#include "boost/optional.hpp"

#include "mongo/bson/oid.h"
#include "mongo/client/sdam/datatypes.h"
#include "mongo/db/repl/optime.h"
#include "mongo/platform/basic.h"
#include "mongo/util/clock_source.h"

namespace mongo::sdam {
class ServerDescription {
public:
    ServerDescription(ServerAddress address) : ServerDescription(address, ServerType::Unknown) {}
    ServerDescription(ServerAddress address, ServerType type)
        : _address(std::move(address)), _type(type) {
        std::transform(_address.begin(), _address.end(), _address.begin(), ::tolower);
    };

    bool operator==(const ServerDescription& other) const;
    bool operator!=(const ServerDescription& other) const;

    const ServerAddress& getAddress() const;
    const boost::optional<std::string>& getError() const;
    const boost::optional<OpLatency>& getRtt() const;
    const boost::optional<Date_t>& getLastWriteDate() const;
    const boost::optional<repl::OpTime>& getOpTime() const;
    ServerType getType() const;
    const boost::optional<ServerAddress>& getMe() const;
    const std::set<ServerAddress>& getHosts() const;
    const std::set<ServerAddress>& getPassives() const;
    const std::set<ServerAddress>& getArbiters() const;
    const std::map<std::string, std::string>& getTags() const;
    const boost::optional<std::string>& getSetName() const;
    const boost::optional<int>& getSetVersion() const;
    const boost::optional<OID>& getElectionId() const;
    const boost::optional<ServerAddress>& getPrimary() const;
    const Date_t getLastUpdateTime() const;
    const boost::optional<int>& getLogicalSessionTimeoutMinutes() const;
    int getMinWireVersion() const;
    int getMaxWireVersion() const;

    bool isDataBearingServer() const;
    BSONObj toBson() const;

private:
    static inline const std::set<ServerType> DATA_SERVER_TYPES{
        ServerType::Mongos, ServerType::RSPrimary, ServerType::RSSecondary, ServerType::Standalone};

    // address: the hostname or IP, and the port number, that the client connects to. Note that this
    // is not the server's ismaster.me field, in the case that the server reports an address
    // different from the address the client uses.
    ServerAddress _address;
    // error: information about the last error related to this server. Default null.
    boost::optional<std::string> _error;
    // roundTripTime: the duration of the ismaster call. Default null.
    boost::optional<OpLatency> _rtt;
    // lastWriteDate: a 64-bit BSON datetime or null. The "lastWriteDate" from the server's most
    // recent ismaster response.
    boost::optional<Date_t> _lastWriteDate;
    // opTime: an ObjectId or null. The last opTime reported by the server; an ObjectId or null.
    // (Only mongos and shard servers record this field when monitoring config servers as replica
    // sets.)
    boost::optional<repl::OpTime> _opTime;
    // (=) type: a ServerType enum value. Default Unknown.
    ServerType _type;
    // (=) minWireVersion, maxWireVersion: the wire protocol version range supported by the server.
    // Both default to 0. Use min and maxWireVersion only to determine compatibility.
    int _minWireVersion = 0;
    int _maxWireVersion = 0;
    // (=) me: The hostname or IP, and the port number, that this server was configured with in the
    // replica set. Default null.
    boost::optional<ServerAddress> _me;
    // (=) hosts, passives, arbiters: Sets of addresses. This server's opinion of the replica set's
    // members, if any. These hostnames are normalized to lower-case. Default empty. The client
    // monitors all three types of servers in a replica set.
    std::set<ServerAddress> _hosts;
    std::set<ServerAddress> _passives;
    std::set<ServerAddress> _arbiters;
    // (=) tags: map from string to string. Default empty.
    std::map<std::string, std::string> _tags;
    // (=) setName: string or null. Default null.
    boost::optional<std::string> _setName;
    // (=) setVersion: integer or null. Default null.
    boost::optional<int> _setVersion;
    // (=) electionId: an ObjectId, if this is a MongoDB 2.6+ replica set member that believes it is
    // primary. See using setVersion and electionId to detect stale primaries. Default null.
    boost::optional<OID> _electionId;
    // (=) primary: an address. This server's opinion of who the primary is. Default null.
    boost::optional<ServerAddress> _primary;
    // lastUpdateTime: when this server was last checked. Default "infinity ago".
    boost::optional<Date_t> _lastUpdateTime;
    // (=) logicalSessionTimeoutMinutes: integer or null. Default null.
    boost::optional<int> _logicalSessionTimeoutMinutes;

    ServerDescription() : ServerDescription("", ServerType::Unknown) {}
    friend class ServerDescriptionBuilder;
};

class ServerDescriptionBuilder {
public:
    ServerDescriptionBuilder() = default;

    /**
     * Build a new ServerDescription according to the rules of the SDAM spec based on the
     * last server description and isMaster response.
     */
    ServerDescriptionBuilder(
        ClockSource* clockSource,
        const IsMasterOutcome& isMasterOutcome,
        boost::optional<ServerDescription> lastServerDescription = boost::none);

    ServerDescription instance() const;
    ServerDescriptionBuilder& withError(const std::string& error);
    ServerDescriptionBuilder& withAddress(const ServerAddress& address);
    ServerDescriptionBuilder& withRtt(const OpLatency& rtt,
                                      boost::optional<OpLatency> lastRtt = boost::none);
    ServerDescriptionBuilder& withLastWriteDate(const Date_t& lastWriteDate);
    ServerDescriptionBuilder& withOpTime(const repl::OpTime opTime);
    ServerDescriptionBuilder& withType(const ServerType type);
    ServerDescriptionBuilder& withMinWireVersion(int minVersion);
    ServerDescriptionBuilder& withMaxWireVersion(int maxVersion);
    ServerDescriptionBuilder& withMe(const ServerAddress& me);
    ServerDescriptionBuilder& withHost(const ServerAddress& host);
    ServerDescriptionBuilder& withPassive(const ServerAddress& passive);
    ServerDescriptionBuilder& withArbiter(const ServerAddress& arbiter);
    ServerDescriptionBuilder& withTag(const std::string key, const std::string value);
    ServerDescriptionBuilder& withSetName(const std::string setName);
    ServerDescriptionBuilder& withSetVersion(const int setVersion);
    ServerDescriptionBuilder& withElectionId(const OID& electionId);
    ServerDescriptionBuilder& withPrimary(const ServerAddress& primary);
    ServerDescriptionBuilder& withLastUpdateTime(const Date_t& lastUpdateTime);
    ServerDescriptionBuilder& withLogicalSessionTimeoutMinutes(
        const int logicalSessionTimeoutMinutes);

private:
    /**
     * Classify the server's type based on the ismaster response.
     * Note: PossiblePrimary server type is not output from this function since this requires global
     * cluster state.
     * @param isMaster - reply document for ismaster command
     */
    void parseTypeFromIsMaster(const BSONObj isMaster);


    void calculateRtt(const OpLatency currentRtt, const boost::optional<OpLatency> lastRtt);
    void saveLastWriteInfo(BSONObj lastWriteBson);
    void storeHostListIfPresent(const std::string key,
                                const BSONObj response,
                                std::set<ServerAddress>& destination);
    void saveHosts(const BSONObj response);
    void saveTags(BSONObj tagsObj);
    void saveElectionId(BSONElement electionId);

    ServerDescription _instance;

    inline static const std::string IS_DB_GRID = "isdbgrid";
    inline static double RTT_ALPHA = 0.2;
};
}  // namespace mongo::sdam