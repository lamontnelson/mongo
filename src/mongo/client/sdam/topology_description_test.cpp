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
#include "mongo/db/wire_version.h"
#include "mongo/unittest/death_test.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
template std::ostream& operator<<(std::ostream& os,
                                  const std::vector<mongo::sdam::ServerAddress>& s);

namespace sdam {
using mongo::operator<<;

class TopologyDescriptionTestFixture : public SdamTestFixture {
protected:
    void assertDefaultConfig(const TopologyDescription& topologyDescription);

    static inline const std::vector<ServerAddress> ONE_SERVER{"foo:1234"};
    static inline const std::vector<ServerAddress> TWO_SERVERS_VARY_CASE{"FoO:1234", "BaR:1234"};
    static inline const std::vector<ServerAddress> TWO_SERVERS_NORMAL_CASE{"foo:1234", "bar:1234"};

    static inline const auto DEFAULT_CONFIG = SdamConfiguration();
    static inline const auto SINGLE_SEED_CONFIG =
        SdamConfiguration(ONE_SERVER, TopologyType::kSingle);
};

void TopologyDescriptionTestFixture::assertDefaultConfig(
    const TopologyDescription& topologyDescription) {
    ASSERT_EQUALS(boost::none, topologyDescription.getSetName());
    ASSERT_EQUALS(boost::none, topologyDescription.getMaxElectionId());

    auto expectedDefaultServers =
        std::vector<ServerDescription>{ServerDescription("localhost:27017")};
    ASSERT_EQUALS(expectedDefaultServers, topologyDescription.getServers());

    ASSERT_EQUALS(true, topologyDescription.isWireVersionCompatible());
    ASSERT_EQUALS(boost::none, topologyDescription.getWireVersionCompatibleError());
    ASSERT_EQUALS(boost::none, topologyDescription.getLogicalSessionTimeoutMinutes());
}

TEST_F(TopologyDescriptionTestFixture, ShouldHaveCorrectDefaultValues) {
    assertDefaultConfig(TopologyDescription(DEFAULT_CONFIG));
    assertDefaultConfig(TopologyDescription());
}

TEST_F(TopologyDescriptionTestFixture, ShouldNormalizeInitialSeedList) {
    auto config = SdamConfiguration(TWO_SERVERS_VARY_CASE);
    TopologyDescription topologyDescription(config);

    std::vector<ServerAddress> expectedAddresses =
        map<ServerAddress, ServerAddress>(TWO_SERVERS_VARY_CASE, [](const ServerAddress& addr) {
            return boost::to_lower_copy(addr);
        });

    std::vector<ServerAddress> serverAddresses = map<ServerDescription, ServerAddress>(
        topologyDescription.getServers(),
        [](const ServerDescription& description) { return description.getAddress(); });

    ASSERT_EQUALS(expectedAddresses, serverAddresses);
}

TEST_F(TopologyDescriptionTestFixture, ShouldAllowTypeSingleWithASingleSeed) {
    TopologyDescription topologyDescription(SINGLE_SEED_CONFIG);

    ASSERT(TopologyType::kSingle == topologyDescription.getType());

    auto servers = map<ServerDescription, ServerAddress>(
        topologyDescription.getServers(),
        [](const ServerDescription& desc) { return desc.getAddress(); });
    ASSERT_EQUALS(ONE_SERVER, servers);
}

TEST_F(TopologyDescriptionTestFixture, DoesNotAllowMultipleSeedsWithSingle) {
    ASSERT_THROWS_CODE(
        {
            auto config = SdamConfiguration(TWO_SERVERS_NORMAL_CASE, TopologyType::kSingle);
            TopologyDescription topologyDescription(config);
        },
        DBException,
        ErrorCodes::InvalidSeedList);
}

TEST_F(TopologyDescriptionTestFixture, ShouldSetTheReplicaSetName) {
    auto expectedSetName = std::string("baz");
    auto config = SdamConfiguration(
        ONE_SERVER, TopologyType::kReplicaSetNoPrimary, mongo::Seconds(10), expectedSetName);
    TopologyDescription topologyDescription(config);
    ASSERT_EQUALS(expectedSetName, *topologyDescription.getSetName());
}

TEST_F(TopologyDescriptionTestFixture, ShouldNotAllowSettingTheReplicaSetNameWithWrongType) {
    ASSERT_THROWS_CODE(
        {
            auto config = SdamConfiguration(
                ONE_SERVER, TopologyType::kUnknown, mongo::Seconds(10), std::string("baz"));
            TopologyDescription topologyDescription(config);
        },
        DBException,
        ErrorCodes::InvalidTopologyType);
}

TEST_F(TopologyDescriptionTestFixture, ShouldNotAllowTopologyTypeRSNoPrimaryWithoutSetName) {
    ASSERT_THROWS_CODE(
        {
            SdamConfiguration(
                ONE_SERVER, TopologyType::kReplicaSetNoPrimary, mongo::Seconds(10), boost::none);
        },
        DBException,
        ErrorCodes::TopologySetNameRequired);
}

TEST_F(TopologyDescriptionTestFixture, ShouldOnlyAllowSingleAndRsNoPrimaryWithSetName) {
    auto topologyTypes = allTopologyTypes();
    topologyTypes.erase(std::remove_if(topologyTypes.begin(),
                                       topologyTypes.end(),
                                       [](const TopologyType& topologyType) {
                                           return topologyType == TopologyType::kSingle ||
                                               topologyType == TopologyType::kReplicaSetNoPrimary;
                                       }),
                        topologyTypes.end());

    for (const auto topologyType : topologyTypes) {
        ASSERT_THROWS_CODE(
            {
                std::cout << "Check TopologyType " << toString(topologyType) << " with setName value." << std::endl;
                auto config = SdamConfiguration(
                    ONE_SERVER, topologyType, mongo::Seconds(10), std::string("setName"));
                // This is here to ensure the compiler acutally generates code for the above statement.
                std::cout << "Test failed for topologyType " << config.getInitialType() << std::endl;
                MONGO_UNREACHABLE
            },
            DBException,
            ErrorCodes::InvalidTopologyType);
    }
}

TEST_F(TopologyDescriptionTestFixture, ShouldDefaultHeartbeatToTenSecs) {
    SdamConfiguration config;
    ASSERT_EQUALS(mongo::Seconds(10), config.getHeartBeatFrequency());
}

TEST_F(TopologyDescriptionTestFixture, ShouldAllowSettingTheHeartbeatFrequency) {
    SdamConfiguration config(boost::none, TopologyType::kUnknown, mongo::Milliseconds(20 * 1000));
    ASSERT_EQUALS(mongo::Seconds(20), config.getHeartBeatFrequency());
}

TEST_F(TopologyDescriptionTestFixture, ShouldNotAllowChangingTheHeartbeatFrequencyBelow500Ms) {
    ASSERT_THROWS_CODE(
        { SdamConfiguration config(boost::none, TopologyType::kUnknown, mongo::Milliseconds(1)); },
        DBException,
        ErrorCodes::InvalidHeartBeatFrequency);
}

TEST_F(TopologyDescriptionTestFixture,
       ShouldSetWireCompatibilityErrorForMinWireVersionWhenMinWireVersionIsGreater) {
    const auto outgoingMaxWireVersion = WireSpec::instance().outgoing.maxWireVersion;
    const auto config = SdamConfiguration(ONE_SERVER, TopologyType::kUnknown, mongo::Seconds(10));
    TopologyDescription topologyDescription(config);
    const auto serverDescriptionMinVersion = ServerDescriptionBuilder()
                                                 .withAddress(ONE_SERVER[0])
                                                 .withMe(ONE_SERVER[0])
                                                 .withType(ServerType::kRSSecondary)
                                                 .withMinWireVersion(outgoingMaxWireVersion + 1)
                                                 .instance();

    ASSERT_EQUALS(boost::none, topologyDescription.getWireVersionCompatibleError());
    auto event = std::make_shared<UpdateServerDescriptionEvent>(serverDescriptionMinVersion);
    topologyDescription.getTopologyObserver()->onTopologyStateMachineEvent(event);
    ASSERT_NOT_EQUALS(boost::none, topologyDescription.getWireVersionCompatibleError());
    // TODO: assert exact text
}

TEST_F(TopologyDescriptionTestFixture,
       ShouldSetWireCompatibilityErrorForMinWireVersionWhenMaxWireVersionIsLess) {
    const auto outgoingMinWireVersion = WireSpec::instance().outgoing.minWireVersion;
    const auto config = SdamConfiguration(ONE_SERVER, TopologyType::kUnknown, mongo::Seconds(10));
    TopologyDescription topologyDescription(config);
    const auto serverDescriptionMaxVersion = ServerDescriptionBuilder()
                                                 .withAddress(ONE_SERVER[0])
                                                 .withMe(ONE_SERVER[0])
                                                 .withType(ServerType::kRSSecondary)
                                                 .withMaxWireVersion(outgoingMinWireVersion - 1)
                                                 .instance();

    ASSERT_EQUALS(boost::none, topologyDescription.getWireVersionCompatibleError());
    auto event = std::make_shared<UpdateServerDescriptionEvent>(serverDescriptionMaxVersion);
    topologyDescription.getTopologyObserver()->onTopologyStateMachineEvent(event);
    ASSERT_NOT_EQUALS(boost::none, topologyDescription.getWireVersionCompatibleError());
    // TODO: assert exact text
}

TEST_F(TopologyDescriptionTestFixture, ShouldNotSetWireCompatibilityErrorWhenServerTypeIsUnknown) {
    const auto outgoingMinWireVersion = WireSpec::instance().outgoing.minWireVersion;
    const auto config = SdamConfiguration(ONE_SERVER, TopologyType::kUnknown, mongo::Seconds(10));
    TopologyDescription topologyDescription(config);
    const auto serverDescriptionMaxVersion =
        ServerDescriptionBuilder().withMaxWireVersion(outgoingMinWireVersion - 1).instance();

    ASSERT_EQUALS(boost::none, topologyDescription.getWireVersionCompatibleError());
    auto event = std::make_shared<UpdateServerDescriptionEvent>(serverDescriptionMaxVersion);
    topologyDescription.getTopologyObserver()->onTopologyStateMachineEvent(event);
    ASSERT_EQUALS(boost::none, topologyDescription.getWireVersionCompatibleError());
}
};  // namespace sdam
};  // namespace mongo
