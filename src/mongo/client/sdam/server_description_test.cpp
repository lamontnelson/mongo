#include <ostream>

#include "mongo/client/sdam/server_description.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
using namespace sdam;
using namespace std;

ostream& operator<<(ostream& os, const ServerDescription& description) {
    os << description.getAddress();
    os << static_cast<int>(description.getType());
    return os;
}

TEST(ServerDescriptionTest, ShouldCompareDefaultValuesAsEqual) {
    ServerDescription a("foo:1234", ServerType::Standalone);
    ServerDescription b("foo:1234", ServerType::Standalone);
    ASSERT_EQUALS(a, b);
}

TEST(ServerDescriptionTest, ShouldCompareDifferentAddressAsEqual) {
    ServerDescription a("foo:1234", ServerType::Standalone);
    ServerDescription b("bar:1234", ServerType::Standalone);
    ASSERT_EQUALS(a, b);
}
};  // namespace mongo
