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
#include "mongo/base/checked_cast.h"
#include "mongo/client/sdam/sdam_test_base.h"
#include "mongo/client/sdam/server_description.h"
#include "mongo/client/sdam/topology_description.h"
#include "mongo/client/sdam/topology_state_machine.h"

namespace mongo::sdam {
class TopologyStateMachineTestFixture : public SdamTestFixture {
protected:
    static inline const auto REPLICA_SET_NAME = "replica_set";
    static inline const auto LOCAL_SERVER = "localhost:123";
    static inline const auto LOCAL_SERVER2 = "localhost:456";
    static inline const auto TWO_SEED_CONFIG =
        SdamConfiguration(std::vector<ServerAddress>{LOCAL_SERVER, LOCAL_SERVER2});
    static inline const auto TWO_SEED_REPLICA_SET_NO_PRIMARY_CONFIG =
        SdamConfiguration(std::vector<ServerAddress>{LOCAL_SERVER, LOCAL_SERVER2},
                          TopologyType::kReplicaSetNoPrimary);
    static inline const auto SINGLE_CONFIG =
        SdamConfiguration(std::vector<ServerAddress>{LOCAL_SERVER}, TopologyType::kSingle);

    // Given we in 'starting' state with initial config 'initialConfig'. We receive a
    // ServerDescription with type 'incoming', and expected the ending topology state to be
    // 'ending'.
    struct TopologyTypeTestCase {
        SdamConfiguration initialConfig;
        TopologyType starting;
        ServerType incoming;
        TopologyType ending;
    };

    struct StateMachineObserver : public TopologyObserver {
        void onTopologyStateMachineEvent(std::shared_ptr<TopologyStateMachineEvent> e) override {
            switch (e->type) {
                case TopologyStateMachineEventType::kTopologyTypeChange:
                    topologyType = std::dynamic_pointer_cast<TopologyTypeChangeEvent>(e)->newType;
                    break;
                case TopologyStateMachineEventType::kNewSetName:
                    setName = std::dynamic_pointer_cast<NewSetNameEvent>(e)->newSetName;
                    std::cout << "new set name: " << (setName ? *setName : std::string(""))
                              << std::endl;
                    break;
                case TopologyStateMachineEventType::kUpdateServerType: {
                    const std::shared_ptr<UpdateServerTypeEvent>& event =
                        std::dynamic_pointer_cast<UpdateServerTypeEvent>(e);
                    std::cout << "new server type: " << event->newServerType << std::endl;
                    serverTypes[event->serverDescription.getAddress()] = event->newServerType;
                    break;
                }
                case TopologyStateMachineEventType::kNewServerDescription: {
                    auto& newServerDescription =
                        std::dynamic_pointer_cast<NewServerDescriptionEvent>(e)
                            ->newServerDescription;
                    std::cout << "new server desc:" << newServerDescription << std::endl;
                    newDescriptions.push_back(newServerDescription);
                    break;
                }
                case TopologyStateMachineEventType::kUpdateServerDescription: {
                    const auto& updateServerDescription =
                        std::dynamic_pointer_cast<UpdateServerDescriptionEvent>(e)
                            ->updatedServerDescription;
                    std::cout << "update server desc:" << updateServerDescription << std::endl;
                    updatedDescriptions.push_back(updateServerDescription);
                    break;
                }
                case TopologyStateMachineEventType::kRemoveServerDescription: {
                    auto& removeServerDescription =
                        std::dynamic_pointer_cast<RemoveServerDescriptionEvent>(e)
                            ->removedServerDescription;
                    std::cout << "remove server desc:" << removeServerDescription << std::endl;
                    removedDescriptions.push_back(removeServerDescription);
                    break;
                }
                case TopologyStateMachineEventType::kNewMaxElectionId:
                    maxElectionId =
                        std::dynamic_pointer_cast<NewMaxElectionIdEvent>(e)->newMaxElectionId;
                    std::cout << "new max election id: " << maxElectionId << std::endl;
                    break;
                case TopologyStateMachineEventType::kNewMaxSetVersion:
                    maxSetVersion =
                        std::dynamic_pointer_cast<NewMaxSetVersionEvent>(e)->newMaxSetVersion;
                    std::cout << "new max set version: " << maxSetVersion << std::endl;
                    break;
            }
        }

        TopologyType topologyType = TopologyType::kUnknown;
        boost::optional<std::string> setName;
        OID maxElectionId = OID::max();
        int maxSetVersion = -1;
        std::vector<ServerDescription> newDescriptions;
        std::vector<ServerDescription> updatedDescriptions;
        std::vector<ServerDescription> removedDescriptions;
        std::map<ServerAddress, ServerType> serverTypes;
    };

    // This function sets up the test scenario defined by the given TopologyTypeTestCase. It
    // simulates receiving a ServerDescription, and asserts that the final topology type is in the
    // correct state.
    void assertTopologyTypeTestCase(TopologyTypeTestCase testCase) {
        auto observer = std::shared_ptr<StateMachineObserver>(new StateMachineObserver());
        TopologyStateMachine stateMachine(testCase.initialConfig);

        // setup the initial state
        TopologyDescription topologyDescription(testCase.initialConfig);
        topologyDescription.setType(testCase.starting);
        stateMachine.addObserver(topologyDescription.getTopologyObserver());
        stateMachine.addObserver(checked_pointer_cast<TopologyObserver>(observer));
        observer->topologyType = testCase.starting;

        // create new ServerDescription and
        auto serverDescriptionBuilder =
            ServerDescriptionBuilder().withType(testCase.incoming).withAddress(LOCAL_SERVER);

        // update the known hosts in the ServerDescription
        if (testCase.initialConfig.getSeedList()) {
            for (auto address : *testCase.initialConfig.getSeedList()) {
                serverDescriptionBuilder.withHost(address);
            }
        }

        // set the primary if we are creating one
        if (testCase.incoming == ServerType::kRSPrimary) {
            serverDescriptionBuilder.withPrimary(LOCAL_SERVER);
        }

        // set the replica set name if appropriate
        const std::vector<ServerType>& replicaSetServerTypes = std::vector<ServerType>{
            ServerType::kRSOther, ServerType::kRSSecondary, ServerType::kRSArbiter};
        if (std::find(replicaSetServerTypes.begin(),
                      replicaSetServerTypes.end(),
                      testCase.incoming) != replicaSetServerTypes.end()) {
            serverDescriptionBuilder.withSetName(REPLICA_SET_NAME);
        }

        const auto serverDescription = serverDescriptionBuilder.instance();

        // simulate the ServerDescription being received
        stateMachine.nextServerDescription(topologyDescription, serverDescription);

        ASSERT_EQUALS(observer->topologyType, testCase.ending);
    }

    std::vector<ServerType> allServerTypesExceptPrimary() {
        auto allExceptPrimary = allServerTypes();
        allExceptPrimary.erase(
            std::remove_if(allExceptPrimary.begin(),
                           allExceptPrimary.end(),
                           [](const ServerType t) { return t == ServerType::kRSPrimary; }),
            allExceptPrimary.end());
        return allExceptPrimary;
    }
};

TEST_F(TopologyStateMachineTestFixture, ShouldInstallServerDescriptionInSingleTopology) {
    auto observer = std::shared_ptr<StateMachineObserver>(new StateMachineObserver());
    TopologyDescription topologyDescription(SINGLE_CONFIG);
    TopologyStateMachine stateMachine(SINGLE_CONFIG);
    stateMachine.addObserver(topologyDescription.getTopologyObserver());
    stateMachine.addObserver(observer);
    auto serverDescription = ServerDescriptionBuilder()
                                 .withAddress(LOCAL_SERVER)
                                 .withMe("foo:1234")
                                 .withType(ServerType::kStandalone)
                                 .instance();

    stateMachine.nextServerDescription(topologyDescription, serverDescription);
    ASSERT_EQUALS(std::vector<ServerDescription>{serverDescription}, observer->updatedDescriptions);
    ASSERT_EQUALS(TopologyType::kSingle, topologyDescription.getType());
}


TEST_F(TopologyStateMachineTestFixture, ShouldInstallNewServerDescription) {
    auto observer = std::shared_ptr<StateMachineObserver>(new StateMachineObserver());
    TopologyDescription topologyDescription(TWO_SEED_CONFIG);
    TopologyStateMachine stateMachine(TWO_SEED_CONFIG);
    stateMachine.addObserver(topologyDescription.getTopologyObserver());
    stateMachine.addObserver(observer);
    auto serverDescription =
        ServerDescriptionBuilder().withAddress("serverDescription:1234").instance();
    stateMachine.nextServerDescription(topologyDescription, serverDescription);
    ASSERT_EQUALS(serverDescription, observer->newDescriptions.front());
}

// The following two tests (ShouldNotUpdateToplogyType, ShouldUpdateToCorrectToplogyType) assert
// that the topology type is correct given an initial state and a ServerType. Together, they
// cover all the cases specified in the SDAM spec here:
// https://github.com/mongodb/specifications/blob/master/source/server-discovery-and-monitoring/server-discovery-and-monitoring.rst#topologytype-table

TEST_F(TopologyStateMachineTestFixture, ShouldNotUpdateToplogyType) {
    using T = TopologyTypeTestCase;

    // test cases that should not change TopologyType
    std::vector<TopologyTypeTestCase> testCases{
        T{TWO_SEED_CONFIG, TopologyType::kUnknown, ServerType::kUnknown, TopologyType::kUnknown},
        T{TWO_SEED_CONFIG, TopologyType::kUnknown, ServerType::kStandalone, TopologyType::kUnknown},
        T{TWO_SEED_CONFIG, TopologyType::kUnknown, ServerType::kRSGhost, TopologyType::kUnknown},
        T{TWO_SEED_CONFIG,
          TopologyType::kReplicaSetNoPrimary,
          ServerType::kUnknown,
          TopologyType::kReplicaSetNoPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kReplicaSetNoPrimary,
          ServerType::kUnknown,
          TopologyType::kReplicaSetNoPrimary},
    };
    for (auto serverType : allServerTypes()) {
        testCases.push_back(
            T{TWO_SEED_CONFIG, TopologyType::kSharded, serverType, TopologyType::kSharded});
    }

    const auto& allExceptPrimary = allServerTypesExceptPrimary();
    for (auto serverType : allExceptPrimary) {
        testCases.push_back(T{TWO_SEED_CONFIG,
                              TopologyType::kReplicaSetNoPrimary,
                              serverType,
                              TopologyType::kReplicaSetNoPrimary});
    }

    int count = 0;
    for (auto testCase : testCases) {
        std::cout << "case " << ++count << " starting TopologyType: " << toString(testCase.starting)
                  << "; incoming ServerType: " << toString(testCase.incoming)
                  << "; expect ending TopologyType: " << toString(testCase.ending) << std::endl;

        assertTopologyTypeTestCase(testCase);
    }
}

TEST_F(TopologyStateMachineTestFixture, ShouldUpdateToCorrectToplogyType) {
    using T = TopologyTypeTestCase;

    // test cases that should change TopologyType
    const std::vector<TopologyTypeTestCase> testCases{
        T{TWO_SEED_CONFIG, TopologyType::kUnknown, ServerType::kMongos, TopologyType::kSharded},
        T{TWO_SEED_CONFIG,
          TopologyType::kUnknown,
          ServerType::kRSPrimary,
          TopologyType::kReplicaSetWithPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kUnknown,
          ServerType::kRSSecondary,
          TopologyType::kReplicaSetNoPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kUnknown,
          ServerType::kRSArbiter,
          TopologyType::kReplicaSetNoPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kUnknown,
          ServerType::kRSOther,
          TopologyType::kReplicaSetNoPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kReplicaSetNoPrimary,
          ServerType::kRSPrimary,
          TopologyType::kReplicaSetWithPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kReplicaSetWithPrimary,
          ServerType::kUnknown,
          TopologyType::kReplicaSetNoPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kReplicaSetWithPrimary,
          ServerType::kStandalone,
          TopologyType::kReplicaSetNoPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kReplicaSetWithPrimary,
          ServerType::kMongos,
          TopologyType::kReplicaSetNoPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kReplicaSetWithPrimary,
          ServerType::kRSPrimary,
          TopologyType::kReplicaSetWithPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kReplicaSetWithPrimary,
          ServerType::kRSSecondary,
          TopologyType::kReplicaSetNoPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kReplicaSetWithPrimary,
          ServerType::kRSOther,
          TopologyType::kReplicaSetNoPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kReplicaSetWithPrimary,
          ServerType::kRSArbiter,
          TopologyType::kReplicaSetNoPrimary},
        T{TWO_SEED_CONFIG,
          TopologyType::kReplicaSetWithPrimary,
          ServerType::kRSGhost,
          TopologyType::kReplicaSetNoPrimary}};

    int count = 0;
    for (auto testCase : testCases) {
        std::cout << "case " << ++count << " starting TopologyType: " << toString(testCase.starting)
                  << "; incoming ServerType: " << toString(testCase.incoming)
                  << "; expect ending TopologyType: " << toString(testCase.ending) << std::endl;

        assertTopologyTypeTestCase(testCase);
    }
}

TEST_F(TopologyStateMachineTestFixture,
       ShouldChangeServerDescriptionTypeToPossiblePrimaryWhenTopologyTypeIsReplicaSetNoPrimary) {
    const SdamConfiguration& config = TWO_SEED_REPLICA_SET_NO_PRIMARY_CONFIG;
    const auto follower = (*config.getSeedList())[0];
    const auto expectedPossiblePrimary = (*config.getSeedList())[1];

    TopologyDescription topologyDescription(config);
    TopologyStateMachine stateMachine(config);
    auto observer = std::shared_ptr<StateMachineObserver>(new StateMachineObserver());
    stateMachine.addObserver(observer);

    const auto serverDescriptionFollowsPrimary =
        ServerDescriptionBuilder(*topologyDescription.getServerByAdress(follower))
            .withType(ServerType::kRSSecondary)
            .withMe(follower)
            .withPrimary(expectedPossiblePrimary)
            .instance();

    ASSERT_EQUALS(ServerType::kUnknown,
                  topologyDescription.getServerByAdress(expectedPossiblePrimary)->getType());

    stateMachine.nextServerDescription(topologyDescription, serverDescriptionFollowsPrimary);

    auto it = observer->serverTypes.find(expectedPossiblePrimary);
    ASSERT(it != observer->serverTypes.end());
    ASSERT_EQUALS(ServerType::kPossiblePrimary, observer->serverTypes[expectedPossiblePrimary]);
}
}  // namespace mongo::sdam
