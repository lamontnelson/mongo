#include <ostream>

#include "mongo/client/sdam/server_description.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
using namespace sdam;
using namespace std;

namespace sdam {
ostream& operator<<(ostream& os, const ServerDescription& description) {
    BSONObj obj = description.toBson();
    os << obj.toString();
    return os;
}
}

TEST(ServerDescriptionTest, ShouldNormalizeAddress) {
    ServerDescription a("foo:1234");
    ServerDescription b("FOo:1234");
    ASSERT_EQUALS(a.getAddress(), b.getAddress());
}

TEST(ServerDescriptionEqualityTest, ShouldCompareDefaultValuesAsEqual) {
    ServerDescription a("foo:1234", ServerType::Standalone);
    ServerDescription b("foo:1234", ServerType::Standalone);
    ASSERT_EQUALS(a, b);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareDifferentAddressButSameServerTypeAsEqual) {
    // Note: The SDAM specification does not prescribe how to compare server descriptions with
    // different addresses for equality. We choose that two descriptions are considered equal if
    // their addresses are different.
    ServerDescription a("foo:1234", ServerType::Standalone);
    ServerDescription b("bar:1234", ServerType::Standalone);
    ASSERT_EQUALS(a, b);
}

TEST(ServerDescriptionEqualityTest, ShouldCompareServerTypes) {
    ServerDescription a = ServerDescriptionBuilder().withType(ServerType::Standalone).instance();
    ServerDescription b = ServerDescriptionBuilder().withType(ServerType::RSSecondary).instance();
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
    ServerDescription a =
        ServerDescriptionBuilder().withLogicalSessionTimeoutMinutes(1).instance();
    ServerDescription b =
        ServerDescriptionBuilder().withLogicalSessionTimeoutMinutes(2).instance();
    ASSERT_NOT_EQUALS(a, b);
    ASSERT_EQUALS(a, a);
}
};  // namespace mongo
