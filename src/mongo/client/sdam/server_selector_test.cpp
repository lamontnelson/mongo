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
#include "mongo/client/sdam/server_selector.h"

#include <boost/optional/optional_io.hpp>

#include "mongo/client/sdam/sdam_test_base.h"
#include "mongo/client/sdam/server_description_builder.h"
#include "mongo/client/sdam/topology_description.h"
#include "mongo/client/sdam/topology_manager.h"
#include "mongo/db/wire_version.h"
#include "mongo/util/system_clock_source.h"

namespace mongo::sdam {

class ServerSelectorTestFixture : public SdamTestFixture {
public:
    static inline const auto clockSource = SystemClockSource::get();
    static inline const auto sdamConfiguration = SdamConfiguration();
    static inline const auto selectionConfig =
        ServerSelectionConfiguration(Milliseconds(1), Milliseconds(10));
    static inline auto selector = SdamServerSelector(selectionConfig);

    ServerDescriptionPtr make_with_latency(IsMasterRTT latency,
                                           ServerAddress address,
                                           ServerType serverType = ServerType::kRSPrimary) {
        return ServerDescriptionBuilder()
            .withType(serverType)
            .withAddress(address)
            .withRtt(latency)
            .withMinWireVersion(WireVersion::SUPPORTS_OP_MSG)
            .withMaxWireVersion(WireVersion::LATEST_WIRE_VERSION)
            .instance();
    }
};

TEST_F(ServerSelectorTestFixture, ShouldFilterCorrectlyByLatencyWindow) {
    const auto delta = Milliseconds(10);
    const auto windowWidth = Milliseconds(100);
    const auto lowerBound = Milliseconds(100);

    auto window = LatencyWindow(lowerBound, windowWidth);

    std::vector<ServerDescriptionPtr> servers = {
        make_with_latency(window.lower - delta, "less"),
        make_with_latency(window.lower, "boundary-lower"),
        make_with_latency(window.lower + delta, "within"),
        make_with_latency(window.upper, "boundary-upper"),
        make_with_latency(window.upper + delta, "greater")};

    window.filterServers(&servers);

    ASSERT_EQ(3, servers.size());
    ASSERT_EQ("boundary-lower", servers[0]->getAddress());
    ASSERT_EQ("within", servers[1]->getAddress());
    ASSERT_EQ("boundary-upper", servers[2]->getAddress());
}

TEST_F(ServerSelectorTestFixture, ShouldThrowOnWireError) {
    auto topologyDescription = std::make_shared<TopologyDescription>(sdamConfiguration);
    auto oldServer = ServerDescriptionBuilder()
                         .withAddress(topologyDescription->getServers().back()->getAddress())
                         .withType(ServerType::kRSPrimary)
                         .withMaxWireVersion(WireVersion::RELEASE_2_4_AND_BEFORE)
                         .withMinWireVersion(WireVersion::RELEASE_2_4_AND_BEFORE)
                         .instance();
    topologyDescription->installServerDescription(oldServer);

    ASSERT(!topologyDescription->isWireVersionCompatible());
    ASSERT_THROWS_CODE(selector.selectServers(topologyDescription, ReadPreferenceSetting()),
                       DBException,
                       ErrorCodes::IncompatibleServerVersion);
}

TEST_F(ServerSelectorTestFixture, ShouldReturnNoneIfTopologyUnknown) {
    auto topologyDescription = std::make_shared<TopologyDescription>(sdamConfiguration);
    ASSERT_EQ(TopologyType ::kUnknown, topologyDescription->getType());
    ASSERT_EQ(boost::none, selector.selectServers(topologyDescription, ReadPreferenceSetting()));
}

TEST_F(ServerSelectorTestFixture, ShouldSelectRandomlyWhenMultipleOptionsAreAvailable) {
    auto topologyDescription = std::make_shared<TopologyDescription>(sdamConfiguration);

    auto smallestLatency = IsMasterRTT(100);

    auto primary = ServerDescriptionBuilder()
                       .withAddress("s0")
                       .withType(ServerType::kRSPrimary)
                       .withRtt(smallestLatency)
                       .withHost("s1")
                       .withHost("s2")
                       .withHost("s3")
                       .withMinWireVersion(WireVersion::SUPPORTS_OP_MSG)
                       .withMaxWireVersion(WireVersion::LATEST_WIRE_VERSION)
                       .instance();
    topologyDescription->installServerDescription(primary);

    auto secondaryInLatencyWindow =
        make_with_latency(smallestLatency + IsMasterRTT(selectionConfig.getLocalThresholdMs() / 2),
                          "s1",
                          ServerType::kRSSecondary);
    topologyDescription->installServerDescription(primary);

    auto secondaryOnBoundaryOfLatencyWindow = make_with_latency(
        smallestLatency + selectionConfig.getLocalThresholdMs(), "s2", ServerType::kRSSecondary);
    topologyDescription->installServerDescription(secondaryOnBoundaryOfLatencyWindow);

    auto secondaryTooFar =
        make_with_latency(smallestLatency + IsMasterRTT(selectionConfig.getLocalThresholdMs() * 2),
                          "s3",
                          ServerType::kRSSecondary);
    topologyDescription->installServerDescription(secondaryTooFar);


    std::map<ServerAddress, int> frequencyInfo{{"s0", 0}, {"s1", 0}, {"s2", 0}, {"s3", 0}};

    const int numIterations = 1000;
    for (int i = 0; i < numIterations; i++) {
        auto server = selector.selectServer(topologyDescription,
                                            ReadPreferenceSetting(ReadPreference::Nearest));
        if (server) {
            frequencyInfo[(*server)->getAddress()]++;
        }
    }

    // we're just checking that a mix got selected without regard to fairness.
    ASSERT(frequencyInfo["s0"]);
    ASSERT(frequencyInfo["s1"]);
    ASSERT(frequencyInfo["s2"]);
    ASSERT_FALSE(frequencyInfo["s3"]);
}

}  // namespace mongo::sdam
