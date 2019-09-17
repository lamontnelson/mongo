#pragma once
#include <map>
#include <set>
#include <utility>

#include "boost/optional.hpp"

#include "mongo/bson/oid.h"
#include "mongo/client/sdam/datatypes.h"
#include "mongo/platform/basic.h"

namespace mongo::sdam {
class ServerDescription {
public:
    ServerDescription(ServerAddress address, ServerType type)
        : _address(std::move(address)), _type(type){};
    bool operator==(const ServerDescription& other) const;

    const ServerAddress& getAddress() const;
    const boost::optional<std::string>& getError() const;
    const boost::optional<OpLatency>& getRtt() const;
    const boost::optional<Date_t>& getLastWriteDate() const;
    const boost::optional<OID>& getOpTime() const;
    ServerType getType() const;
    const boost::optional<ServerAddress>& getMe() const;
    const std::vector<ServerAddress>& getHosts() const;
    const std::vector<ServerAddress>& getPassives() const;
    const std::vector<ServerAddress>& getArbiters() const;
    const std::map<std::string, std::string>& getTags() const;
    const boost::optional<std::string>& getSetName() const;
    const boost::optional<int>& getSetVersion() const;
    const boost::optional<OID>& getElectionId() const;
    const boost::optional<ServerAddress>& getPrimary() const;
    const boost::optional<Date_t>& getLastUpdateTime() const;
    const boost::optional<int>& getLogicalSessionTimeoutMinutes() const;

    bool isDataBearingServer() const;

private:
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
    boost::optional<OID> _opTime;
    // (=) type: a ServerType enum value. Default Unknown.
    ServerType _type;
    // (=) minWireVersion, maxWireVersion: the wire protocol version range supported by the server.
    // Both default to 0. Use min and maxWireVersion only to determine compatibility.
    // (=) me: The hostname or IP, and the port number, that this server was configured with in the
    // replica set. Default null.
    boost::optional<ServerAddress> _me;
    // (=) hosts, passives, arbiters: Sets of addresses. This server's opinion of the replica set's
    // members, if any. These hostnames are normalized to lower-case. Default empty. The client
    // monitors all three types of servers in a replica set.
    std::vector<ServerAddress> _hosts;
    std::vector<ServerAddress> _passives;
    std::vector<ServerAddress> _arbiters;
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
};
}