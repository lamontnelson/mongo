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
#include "mongo/client/sdam/sdam_test_base.h"
#include "mongo/client/sdam/topology_description.h"

#include <boost/optional/optional_io.hpp>

#include "mongo/client/sdam/server_description.h"
#include "mongo/unittest/death_test.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
template std::ostream& operator<<(std::ostream& os,
                                  const std::vector<mongo::sdam::ServerAddress>& s);

namespace sdam {
using mongo::operator<<;

class TopologyDescriptionTestFixture : public SdamTestFixture {
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

    TopologyDescription topologyDescription(TopologyType::kUnknown, seedList);

    std::vector<ServerAddress> expectedAddresses = map<ServerAddress, ServerAddress>(
        seedList, [](const ServerAddress& addr) { return boost::to_lower_copy(addr); });

    std::vector<ServerAddress> serverAddresses = map<ServerDescription, ServerAddress>(
        topologyDescription.getServers(),
        [](const ServerDescription& description) { return description.getAddress(); });

    ASSERT_EQUALS(expectedAddresses, serverAddresses);
}

TEST_F(TopologyDescriptionTestFixture, ShouldAllowTypeSingleWithASingleSeed) {
    auto seedList = std::vector<ServerAddress>{"foo:1234"};
    TopologyDescription topologyDescription(TopologyType::kSingle, seedList);
    ASSERT(TopologyType::kSingle == topologyDescription.getType());

    auto servers = map<ServerDescription, ServerAddress>(
        topologyDescription.getServers(),
        [](const ServerDescription& desc) { return desc.getAddress(); });
    ASSERT_EQUALS(seedList, servers);
}

TEST_F(TopologyDescriptionTestFixture, DoesNotAllowMultipleSeedsWithSingle) {
    auto seedList = std::vector<ServerAddress>{"foo:1234", "bar:1234"};
    ASSERT_THROWS_CODE(
        { TopologyDescription topologyDescription(TopologyType::kSingle, seedList); },
        DBException,
        ErrorCodes::InvalidSeedList);
}

TEST_F(TopologyDescriptionTestFixture, ShouldAllowSettingTheReplicaSetName) {
    auto seedList = std::vector<ServerAddress>{"foo:1234"};
    auto expectedSetName = std::string("baz");
    TopologyDescription topologyDescription(
        TopologyType::kReplicaSetNoPrimary, seedList, expectedSetName);
    ASSERT_EQUALS(expectedSetName, *topologyDescription.getSetName());
}

TEST_F(TopologyDescriptionTestFixture, ShouldNotAllowSettingTheReplicaSetNameWithWrongType) {
    auto seedList = std::vector<ServerAddress>{"foo:1234"};
    auto expectedSetName = std::string("baz");
    ASSERT_THROWS_CODE(
        {
            TopologyDescription topologyDescription(
                TopologyType::kUnknown, seedList, expectedSetName);
            ASSERT_EQUALS(expectedSetName, *topologyDescription.getSetName());
        },
        DBException,
        ErrorCodes::InvalidTopologyType);
}

TEST_F(TopologyDescriptionTestFixture, ShouldDefaultHeartbeatToTenSecs) {
    TopologyDescription topologyDescription(TopologyType::kSingle,
                                            std::vector<ServerAddress>{"foo:1234"});
    ASSERT_EQUALS(mongo::Seconds(10), topologyDescription.getHeartBeatFrequency());
}

TEST_F(TopologyDescriptionTestFixture, ShouldAllowChangingTheHeartbeatFrequency) {
    TopologyDescription topologyDescription(TopologyType::kSingle,
                                            std::vector<ServerAddress>{"foo:1234"});
    topologyDescription.setHeartBeatFrequency(mongo::Milliseconds(20*1000));
    ASSERT_EQUALS(mongo::Seconds(20), topologyDescription.getHeartBeatFrequency());
}

TEST_F(TopologyDescriptionTestFixture, ShouldNotAllowChangingTheHeartbeatFrequencyBelow500Ms) {
    TopologyDescription topologyDescription(TopologyType::kSingle,
                                            std::vector<ServerAddress>{"foo:1234"});
    ASSERT_THROWS_CODE({
        topologyDescription.setHeartBeatFrequency(mongo::Milliseconds(1));
    }, DBException, ErrorCodes::InvalidHeartBeatFrequency);
}
};  // namespace sdam
};  // namespace mongo
