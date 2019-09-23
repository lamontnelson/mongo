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

#include "mongo/client/sdam/topology_description.h"

#include <boost/optional/optional_io.hpp>

#include "mongo/client/sdam/server_description.h"
#include "mongo/unittest/unittest.h"


namespace mongo::sdam {

class TopologyDescriptionTestFixture : public mongo::unittest::Test {
protected:
};

TEST_F(TopologyDescriptionTestFixture, ShouldHaveCorrectDefaultValues) {
    TopologyDescription topologyDescription;
    ASSERT_EQUALS(boost::none, topologyDescription.getSetName());
    ASSERT_EQUALS(boost::none, topologyDescription.getMaxElectionId());

    auto expectedDefaultServers =
        std::vector<ServerDescription>{ServerDescription("localhost:27017")};
    ASSERT_EQUALS(expectedDefaultServers, topologyDescription.getServers());

    ASSERT_EQUALS(true, topologyDescription.isWireVersionCompatible());
    ASSERT_EQUALS(boost::none, topologyDescription.getWireVersionCompatibleError());
    ASSERT_EQUALS(boost::none, topologyDescription.getLogicalSessionTimeoutMinutes());
}

TEST_F(TopologyDescriptionTestFixture, ShouldNormalizeInitialSeedList) {
    auto seedList = std::vector<ServerAddress>{"FoO:1234", "BaR:1234"};

    TopologyDescription topologyDescription(TopologyType::kUnknown, seedList, "");

    std::vector<ServerAddress> expectedAddresses;
    std::transform(seedList.begin(),
                   seedList.end(),
                   std::back_inserter(expectedAddresses),
                   [](auto address) { return boost::to_lower_copy(address); });

    std::vector<ServerAddress> serverAddresses;
    std::transform(topologyDescription.getServers().begin(),
                   topologyDescription.getServers().end(),
                   std::back_inserter(serverAddresses),
                   [](const ServerDescription& description) { return description.getAddress(); });

    ASSERT(expectedAddresses == serverAddresses);
}
};  // namespace mongo::sdam
