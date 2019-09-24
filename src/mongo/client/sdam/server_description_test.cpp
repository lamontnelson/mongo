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

#include <boost/algorithm/string.hpp>
#include <boost/optional/optional_io.hpp>
#include <mongo/db/jsobj.h>
#include <ostream>
#include <set>

#include "mongo/client/sdam/sdam_test_base.h"
#include "mongo/client/sdam/server_description.h"
#include "mongo/db/repl/optime.h"
#include "mongo/util/system_clock_source.h"

namespace mongo {
using namespace std;
template std::ostream& operator<<(std::ostream& os,
                                  const std::vector<mongo::sdam::ServerDescription>& v);
template std::ostream& operator<<(std::ostream& os, const std::set<std::string>& s);
template std::ostream& operator<<(std::ostream& os, const std::map<std::string, std::string>& m);

namespace sdam {
using mongo::operator<<;

ostream& operator<<(ostream& os, ServerType serverType) {
    os << toString(serverType);
    return os;
}


TEST(ServerDescriptionTest, ShouldNormalizeAddress) {
    ServerDescription a("foo:1234");
    ServerDescription b("FOo:1234");
    ASSERT_EQUALS(a.getAddress(), b.getAddress());
}

TEST(ServerDescriptionEqualityTest, ShouldCompareDefaultValuesAsEqual) {
    ServerDescription a("foo:1234", ServerType::kStandalone);
    ServerDescription b("foo:1234", ServerType::kStandalone);
    ASSERT_EQUALS(a, b);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareDifferentAddressButSameServerTypeAsEqual) {
    // Note: The SDAM specification does not prescribe how to compare server descriptions with
    // different addresses for equality. We choose that two descriptions are considered equal if
    // their addresses are different.
    ServerDescription a("foo:1234", ServerType::kStandalone);
    ServerDescription b("bar:1234", ServerType::kStandalone);
    ASSERT_EQUALS(a, b);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareServerTypes) {
    ServerDescription a = ServerDescriptionBuilder().withType(ServerType::kStandalone).instance();
    ServerDescription b = ServerDescriptionBuilder().withType(ServerType::kRSSecondary).instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareMinWireVersion) {
    ServerDescription a = ServerDescriptionBuilder().withMinWireVersion(1).instance();
    ServerDescription b = ServerDescriptionBuilder().withMinWireVersion(2).instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareMaxWireVersion) {
    ServerDescription a = ServerDescriptionBuilder().withMaxWireVersion(1).instance();
    ServerDescription b = ServerDescriptionBuilder().withMaxWireVersion(2).instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareMeValues) {
    ServerDescription a = ServerDescriptionBuilder().withMe("foo").instance();
    ServerDescription b = ServerDescriptionBuilder().withMe("bar").instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareHosts) {
    ServerDescription a = ServerDescriptionBuilder().withHost("foo").instance();
    ServerDescription b = ServerDescriptionBuilder().withHost("bar").instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldComparePassives) {
    ServerDescription a = ServerDescriptionBuilder().withPassive("foo").instance();
    ServerDescription b = ServerDescriptionBuilder().withPassive("bar").instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareArbiters) {
    ServerDescription a = ServerDescriptionBuilder().withArbiter("foo").instance();
    ServerDescription b = ServerDescriptionBuilder().withArbiter("bar").instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareMultipleHostsOrderDoesntMatter) {
    ServerDescription a = ServerDescriptionBuilder().withHost("foo").withHost("bar").instance();
    ServerDescription b = ServerDescriptionBuilder().withHost("bar").withHost("foo").instance();
    ASSERT_EQUALS(a, b);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareMultiplePassivesOrderDoesntMatter) {
    ServerDescription a =
        ServerDescriptionBuilder().withPassive("foo").withPassive("bar").instance();
    ServerDescription b =
        ServerDescriptionBuilder().withPassive("bar").withPassive("foo").instance();
    ASSERT_EQUALS(a, b);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareMultipleArbitersOrderDoesntMatter) {
    ServerDescription a =
        ServerDescriptionBuilder().withArbiter("foo").withArbiter("bar").instance();
    ServerDescription b =
        ServerDescriptionBuilder().withArbiter("bar").withArbiter("foo").instance();
    ASSERT_EQUALS(a, b);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareTags) {
    ServerDescription a = ServerDescriptionBuilder().withTag("foo", "bar").instance();
    ServerDescription b = ServerDescriptionBuilder().withTag("baz", "buz").instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareSetName) {
    ServerDescription a = ServerDescriptionBuilder().withSetName("foo").instance();
    ServerDescription b = ServerDescriptionBuilder().withSetName("bar").instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareSetVersion) {
    ServerDescription a = ServerDescriptionBuilder().withSetVersion(1).instance();
    ServerDescription b = ServerDescriptionBuilder().withSetVersion(2).instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareElectionId) {
    ServerDescription a = ServerDescriptionBuilder().withElectionId(OID::max()).instance();
    ServerDescription b =
        ServerDescriptionBuilder().withElectionId(OID("000000000000000000000000")).instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldComparePrimary) {
    ServerDescription a = ServerDescriptionBuilder().withPrimary("foo:1234").instance();
    ServerDescription b = ServerDescriptionBuilder().withPrimary("bar:1234").instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareLogicalSessionTimeout) {
    ServerDescription a = ServerDescriptionBuilder().withLogicalSessionTimeoutMinutes(1).instance();
    ServerDescription b = ServerDescriptionBuilder().withLogicalSessionTimeoutMinutes(2).instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}


class ServerDescriptionBuilderTestFixture : public SdamTestFixture {
protected:
    // returns a set containing the elements in the given bson array with lowercase values.
    std::set<std::string> toHostSet(std::vector<BSONElement> bsonArray) {
        return mapSet<BSONElement, std::string>(bsonArray, [](const BSONElement& e) {return boost::to_lower_copy(e.String());});
    }

    std::map<std::string, std::string> toStringMap(BSONObj bsonObj) {
        std::map<std::string, std::string> result;
        const auto keys = bsonObj.getFieldNames<std::set<std::string>>();
        std::transform(keys.begin(),
                       keys.end(),
                       std::inserter(result, result.begin()),
                       [bsonObj](const std::string& key) {
                           return std::pair<const std::string, std::string>(
                               key, bsonObj.getStringField(key));
                       });
        return result;
    }

    static BSONObjBuilder okBuilder() {
        return std::move(BSONObjBuilder().append("ok", 1));
    }

    inline static const auto clockSource = SystemClockSource::get();

    inline static const auto BSON_OK = okBuilder().obj();
    inline static const auto BSON_MISSING_OK = BSONObjBuilder().obj();
    inline static const auto BSON_MONGOS = okBuilder().append("msg", "isdbgrid").obj();
    inline static const auto BSON_RSPRIMARY =
        okBuilder().append("ismaster", true).append("setName", "foo").obj();
    inline static const auto BSON_RSSECONDARY =
        okBuilder().append("secondary", true).append("setName", "foo").obj();
    inline static const auto BSON_RSARBITER =
        okBuilder().append("arbiterOnly", true).append("setName", "foo").obj();
    inline static const auto BSON_RSOTHER =
        okBuilder().append("hidden", true).append("setName", "foo").obj();
    inline static const auto BSON_RSGHOST = okBuilder().append("isreplicaset", true).obj();
    inline static const auto BSON_WIRE_VERSION =
        okBuilder().append("minWireVersion", 1).append("maxWireVersion", 2).obj();
    inline static const auto BSON_TAGS =
        okBuilder()
            .append("tags", BSONObjBuilder().append("foo", "bar").append("baz", "buz").obj())
            .obj();
    inline static const mongo::repl::OpTime OP_TIME =
        mongo::repl::OpTime(Timestamp(1568848910), 24);
    inline static const Date_t LAST_WRITE_DATE =
        dateFromISOString("2019-09-18T23:21:50Z").getValue();
    inline static const auto BSON_LAST_WRITE =
        okBuilder()
            .append("lastWrite",
                    BSONObjBuilder()
                        .appendTimeT("lastWriteDate", LAST_WRITE_DATE.toTimeT())
                        .append("opTime", OP_TIME.toBSON())
                        .obj())
            .obj();
    inline static const auto BSON_HOSTNAMES = okBuilder()
                                                  .append("me", "Me:1234")
                                                  .appendArray("hosts",
                                                               BSON_ARRAY("Foo:1234"
                                                                          << "Bar:1234"))
                                                  .appendArray("arbiters",
                                                               BSON_ARRAY("Baz:1234"
                                                                          << "Buz:1234"))
                                                  .appendArray("passives",
                                                               BSON_ARRAY("Biz:1234"
                                                                          << "Boz:1234"))
                                                  .obj();
    inline static const auto BSON_SET_VERSION_NAME =
        okBuilder().append("setVersion", 1).append("setName", "bar").obj();
    inline static const auto BSON_ELECTION_ID = okBuilder().append("electionId", OID::max()).obj();
    inline static const auto BSON_PRIMARY = okBuilder().append("primary", "foo:1234").obj();
    inline static const auto BSON_LOGICAL_SESSION_TIMEOUT =
        okBuilder().append("logicalSessionTimeoutMinutes", 1).obj();
};

TEST_F(ServerDescriptionBuilderTestFixture, ShouldParseTypeAsUnknownForIsMasterError) {
    auto response = IsMasterOutcome("foo:1234", "an error occurred");
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(ServerType::kUnknown, description.getType());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldParseTypeAsUnknownIfOkMissing) {
    auto response = IsMasterOutcome("foo:1234", BSON_MISSING_OK, IsMasterLatency::min());
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(ServerType::kUnknown, description.getType());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldParseTypeAsStandalone) {
    // No "msg: isdbgrid", no setName, and no "isreplicaset: true".
    auto response = IsMasterOutcome("foo:1234", BSON_OK, IsMasterLatency::min());
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(ServerType::kStandalone, description.getType());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldParseTypeAsMongos) {
    // contains "msg: isdbgrid"
    auto response = IsMasterOutcome("foo:1234", BSON_MONGOS, IsMasterLatency::min());
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(ServerType::kMongos, description.getType());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldParseTypeAsRSPrimary) {
    // "ismaster: true", "setName" in response
    auto response = IsMasterOutcome("foo:1234", BSON_RSPRIMARY, IsMasterLatency::min());
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(ServerType::kRSPrimary, description.getType());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldParseTypeAsRSSecondary) {
    // "secondary: true", "setName" in response
    auto response = IsMasterOutcome("foo:1234", BSON_RSSECONDARY, IsMasterLatency::min());
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(ServerType::kRSSecondary, description.getType());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldParseTypeAsArbiter) {
    // "arbiterOnly: true", "setName" in response.
    auto response = IsMasterOutcome("foo:1234", BSON_RSARBITER, IsMasterLatency::min());
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(ServerType::kRSArbiter, description.getType());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldParseTypeAsOther) {
    // "hidden: true", "setName" in response, or not primary, secondary, nor arbiter
    auto response = IsMasterOutcome("foo:1234", BSON_RSOTHER, IsMasterLatency::min());
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(ServerType::kRSOther, description.getType());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldParseTypeAsGhost) {
    // "isreplicaset: true" in response.
    auto response = IsMasterOutcome("foo:1234", BSON_RSGHOST, IsMasterLatency::min());
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(ServerType::kRSGhost, description.getType());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreErrorDescription) {
    auto errorMsg = "an error occurred";
    auto response = IsMasterOutcome("foo:1234", errorMsg);
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(errorMsg, *description.getError());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreRTTWithNoPreviousLatency) {
    auto response = IsMasterOutcome("foo:1234", BSON_RSPRIMARY, IsMasterLatency::max());
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(IsMasterLatency::max(), *description.getRtt());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreRTTNullWhenServerTypeIsUnknown) {
    auto response = IsMasterOutcome("foo:1234", BSON_MISSING_OK, IsMasterLatency::max());
    auto description = ServerDescriptionBuilder(clockSource, response, boost::none).instance();
    ASSERT_EQUALS(boost::none, description.getRtt());
}

TEST_F(ServerDescriptionBuilderTestFixture,
       ShouldStoreMovingAverageRTTWhenChangingFromOneKnownServerTypeToAnother) {
    auto response = IsMasterOutcome("foo:1234", BSON_RSPRIMARY, mongo::Milliseconds(40));
    auto lastServerDescription = ServerDescriptionBuilder()
                                     .withType(ServerType::kRSSecondary)
                                     .withRtt(mongo::Milliseconds(20))
                                     .instance();
    auto description =
        ServerDescriptionBuilder(clockSource, response, lastServerDescription.getRtt()).instance();
    ASSERT_EQUALS(24, durationCount<mongo::Milliseconds>(*description.getRtt()));

    auto response2 = IsMasterOutcome("foo:1234", BSON_RSPRIMARY, mongo::Milliseconds(30));
    auto description2 =
        ServerDescriptionBuilder(clockSource, response2, description.getRtt()).instance();
    std::cout << durationCount<mongo::Milliseconds>(*description2.getRtt()) << " ms";
    ASSERT_EQUALS(25, durationCount<mongo::Milliseconds>(*description2.getRtt()));
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreLastWriteDate) {
    auto response = IsMasterOutcome("foo:1234", BSON_LAST_WRITE, mongo::Milliseconds(40));
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(LAST_WRITE_DATE, description.getLastWriteDate());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreOpTime) {
    auto response = IsMasterOutcome("foo:1234", BSON_LAST_WRITE, mongo::Milliseconds(40));
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(OP_TIME, description.getOpTime());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreLastUpdateTime) {
    auto testStart = clockSource->now();
    auto response = IsMasterOutcome("foo:1234", BSON_RSPRIMARY, mongo::Milliseconds(40));
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_GREATER_THAN_OR_EQUALS(description.getLastUpdateTime(), testStart);
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreHostNamesAsLowercase) {
    auto response = IsMasterOutcome("FOO:1234", BSON_HOSTNAMES, mongo::Milliseconds(40));
    auto description = ServerDescriptionBuilder(clockSource, response).instance();

    ASSERT_EQUALS("foo:1234", description.getAddress());

    ASSERT_EQUALS(boost::to_lower_copy(std::string(BSON_HOSTNAMES.getStringField("me"))),
                  *description.getMe());

    auto expectedHosts = toHostSet(BSON_HOSTNAMES.getField("hosts").Array());
    ASSERT_EQUALS(expectedHosts, description.getHosts());

    auto expectedPassives = toHostSet(BSON_HOSTNAMES.getField("passives").Array());
    ASSERT_EQUALS(expectedPassives, description.getPassives());

    auto expectedArbiters = toHostSet(BSON_HOSTNAMES.getField("arbiters").Array());
    ASSERT_EQUALS(expectedArbiters, description.getArbiters());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreMinMaxWireVersion) {
    auto response = IsMasterOutcome("foo:1234", BSON_WIRE_VERSION, mongo::Milliseconds(40));
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(BSON_WIRE_VERSION["minWireVersion"].Int(), description.getMinWireVersion());
    ASSERT_EQUALS(BSON_WIRE_VERSION["maxWireVersion"].Int(), description.getMaxWireVersion());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreTags) {
    auto response = IsMasterOutcome("foo:1234", BSON_TAGS, mongo::Milliseconds(40));
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(toStringMap(BSON_TAGS["tags"].Obj()), description.getTags());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreSetVersionAndName) {
    auto response = IsMasterOutcome("foo:1234", BSON_SET_VERSION_NAME, mongo::Milliseconds(40));
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(BSON_SET_VERSION_NAME.getIntField("setVersion"), description.getSetVersion());
    ASSERT_EQUALS(std::string(BSON_SET_VERSION_NAME.getStringField("setName")),
                  description.getSetName());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreElectionId) {
    auto response = IsMasterOutcome("foo:1234", BSON_ELECTION_ID, mongo::Milliseconds(40));
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(BSON_ELECTION_ID.getField("electionId").OID(), description.getElectionId());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStorePrimary) {
    auto response = IsMasterOutcome("foo:1234", BSON_PRIMARY, mongo::Milliseconds(40));
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(std::string(BSON_PRIMARY.getStringField("primary")), description.getPrimary());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreLogicalSessionTimeout) {
    auto response =
        IsMasterOutcome("foo:1234", BSON_LOGICAL_SESSION_TIMEOUT, mongo::Milliseconds(40));
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(BSON_LOGICAL_SESSION_TIMEOUT.getIntField("logicalSessionTimeoutMinutes"),
                  description.getLogicalSessionTimeoutMinutes());
}


TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreServerAddressOnError) {
    auto response = IsMasterOutcome("foo:1234", "an error occurred");
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(std::string("foo:1234"), description.getAddress());
}

TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreCorrectDefaultValuesOnSuccess) {
    auto response = IsMasterOutcome("foo:1234", BSON_OK, mongo::Milliseconds(40));
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(boost::none, description.getError());
    ASSERT_EQUALS(boost::none, description.getLastWriteDate());
    ASSERT_EQUALS(0, description.getMinWireVersion());
    ASSERT_EQUALS(0, description.getMaxWireVersion());
    ASSERT_EQUALS(boost::none, description.getMe());
    ASSERT_EQUALS(static_cast<size_t>(0), description.getHosts().size());
    ASSERT_EQUALS(static_cast<size_t>(0), description.getPassives().size());
    ASSERT_EQUALS(static_cast<size_t>(0), description.getTags().size());
    ASSERT_EQUALS(boost::none, description.getSetName());
    ASSERT_EQUALS(boost::none, description.getSetVersion());
    ASSERT_EQUALS(boost::none, description.getElectionId());
    ASSERT_EQUALS(boost::none, description.getPrimary());
    ASSERT_EQUALS(boost::none, description.getLogicalSessionTimeoutMinutes());
}


TEST_F(ServerDescriptionBuilderTestFixture, ShouldStoreCorrectDefaultValuesOnFailure) {
    auto response = IsMasterOutcome("foo:1234", "an error occurred");
    auto description = ServerDescriptionBuilder(clockSource, response).instance();
    ASSERT_EQUALS(boost::none, description.getLastWriteDate());
    ASSERT_EQUALS(ServerType::kUnknown, description.getType());
    ASSERT_EQUALS(0, description.getMinWireVersion());
    ASSERT_EQUALS(0, description.getMaxWireVersion());
    ASSERT_EQUALS(boost::none, description.getMe());
    ASSERT_EQUALS(static_cast<size_t>(0), description.getHosts().size());
    ASSERT_EQUALS(static_cast<size_t>(0), description.getPassives().size());
    ASSERT_EQUALS(static_cast<size_t>(0), description.getTags().size());
    ASSERT_EQUALS(boost::none, description.getSetName());
    ASSERT_EQUALS(boost::none, description.getSetVersion());
    ASSERT_EQUALS(boost::none, description.getElectionId());
    ASSERT_EQUALS(boost::none, description.getPrimary());
    ASSERT_EQUALS(boost::none, description.getLogicalSessionTimeoutMinutes());
}
}  // namespace sdam
};  // namespace mongo
