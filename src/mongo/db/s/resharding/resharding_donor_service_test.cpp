/**
 *    Copyright (C) 2020-present MongoDB, Inc.
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

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kTest

#include "mongo/platform/basic.h"

#include <boost/optional.hpp>

#include "mongo/client/remote_command_targeter_mock.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/logical_session_cache_noop.h"
#include "mongo/db/repl/storage_interface_impl.h"
#include "mongo/db/repl/storage_interface_mock.h"
#include "mongo/db/s/config/config_server_test_fixture.h"
#include "mongo/db/s/resharding/resharding_coordinator_service.h"
#include "mongo/db/s/resharding/resharding_donor_recipient_common_test.h"
#include "mongo/db/s/resharding/resharding_donor_service.h"
#include "mongo/db/s/resharding_util.h"
#include "mongo/db/s/transaction_coordinator_service.h"
#include "mongo/db/session_catalog_mongod.h"
#include "mongo/executor/remote_command_request.h"
#include "mongo/executor/thread_pool_task_executor_test_fixture.h"
#include "mongo/logv2/log.h"
#include "mongo/s/catalog/type_collection.h"
#include "mongo/s/catalog/type_shard.h"
#include "mongo/s/catalog_cache_loader_mock.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/clock_source_mock.h"


namespace mongo {
namespace {

class ReshardingDonorServiceTest : public ReshardingDonorRecipientCommonTest {
protected:
    std::shared_ptr<ReshardingDonorService::DonorStateMachine> getStateMachineInstace(
        OperationContext* opCtx, ReshardingDonorDocument initialState) {
        auto instanceId = BSON(ReshardingDonorDocument::k_idFieldName << initialState.get_id());
        auto registry = repl::PrimaryOnlyServiceRegistry::get(opCtx->getServiceContext());
        auto service = registry->lookupServiceByName(ReshardingDonorService::kServiceName);
        return ReshardingDonorService::DonorStateMachine::getOrCreate(
            opCtx, service, initialState.toBSON());
    }

    boost::optional<BSONObj> getOplogWriteForDonorDocument(const ReshardingDonorDocument& doc) {
        auto reshardNs = doc.getNss().toString();
        DBDirectClient client(operationContext());
        auto result = client.findOne(kOplogNs, BSON("ns" << reshardNs));
        return (result.isEmpty()) ? boost::none : boost::optional<BSONObj>(result);
    }

    static constexpr auto kOplogNs = "local.oplog.rs";
    static constexpr auto kExpectedO2Type = "reshardFinalOp"_sd;
    static constexpr auto kReshardNs = "db.foo"_sd;
};

TEST_F(ReshardingDonorServiceTest, ShouldWriteFinalOpLogEntryAfterTransitionToPreparingToMirror) {
    ReshardingDonorDocument doc(DonorStateEnum::kPreparingToMirror);
    CommonReshardingMetadata metadata(UUID::gen(),
                                      mongo::NamespaceString(kReshardNs),
                                      UUID::gen(),
                                      KeyPattern(kReshardingKeyPattern));
    doc.setCommonReshardingMetadata(metadata);
    doc.getMinFetchTimestampStruct().setMinFetchTimestamp(Timestamp{0xf00});

    auto donorStateMachine = getStateMachineInstace(operationContext(), doc);
    ASSERT(donorStateMachine);

    assertSoon([&]() {
        const auto maybeOplog = getOplogWriteForDonorDocument(doc);
        if (!maybeOplog)
            return false;

        const auto& oplog = *maybeOplog;
        LOGV2_INFO(5279502, "verify retrieved oplog document", "document"_attr = oplog);

        ASSERT(oplog.hasField("ns"));
        auto actualNs = oplog.getStringField("ns");
        ASSERT_EQUALS(kReshardNs, actualNs);

        ASSERT(oplog.hasField("o2"));
        auto o2 = oplog.getObjectField("o2");
        ASSERT(o2.hasField("type"));
        auto actualType = StringData(o2.getStringField("type"));
        ASSERT_EQUALS(kExpectedO2Type, actualType);
        ASSERT(o2.hasField("reshardingUUID"));
        auto actualReshardingUUIDBson = o2.getField("reshardingUUID");
        auto actualReshardingUUID = UUID::parse(actualReshardingUUIDBson);
        ASSERT_EQUALS(doc.get_id(), actualReshardingUUID);

        return true;
    });
}

}  // namespace
}  // namespace mongo
