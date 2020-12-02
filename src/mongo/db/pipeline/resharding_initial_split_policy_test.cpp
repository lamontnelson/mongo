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

#include "mongo/db/catalog/collection_catalog.h"
#include "mongo/db/pipeline/document_source_mock.h"
#include "mongo/db/pipeline/sharded_agg_helpers.h"
#include "mongo/db/s/config/initial_split_policy.h"
#include "mongo/s/query/sharded_agg_test_fixture.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {

using ReshardingSplitPolicyTest = ShardedAggTestFixture;

const ShardId primaryShardId = ShardId("0");

TEST_F(ReshardingSplitPolicyTest, ShardKeyWithNonDottedFieldAndIdIsNotProjectedSucceeds) {
    auto shardKeyPattern = ShardKeyPattern(BSON("a" << 1));

    auto pipeline =
        Pipeline::parse(ReshardingSplitPolicy::createRawPipeline(
                            shardKeyPattern, 2 /* samplingRatio */, 1 /* numSplitPoints */),
                        expCtx());
    auto mockSource =
        DocumentSourceMock::createForTest({"{_id: 10, a: 15}", "{_id: 3, a: 5}"}, expCtx());
    pipeline->addInitialSource(mockSource.get());

    // We sample all of the documents since numSplitPoints(1) * samplingRatio (2) = 2 and the
    // document source has 2 chunks. So we can assert on the returned values.
    auto next = pipeline->getNext();
    ASSERT_EQUALS(next.get().getField("a").getInt(), 5);
    ASSERT(next.get().getField("_id").missing());
    next = pipeline->getNext();
    ASSERT_EQUALS(next.get().getField("a").getInt(), 15);
    ASSERT(next.get().getField("_id").missing());
    ASSERT(!pipeline->getNext());
}

TEST_F(ReshardingSplitPolicyTest, ShardKeyWithIdFieldIsProjectedSucceeds) {
    auto shardKeyPattern = ShardKeyPattern(BSON("_id" << 1));

    auto pipeline =
        Pipeline::parse(ReshardingSplitPolicy::createRawPipeline(
                            shardKeyPattern, 2 /* samplingRatio */, 1 /* numSplitPoints */),
                        expCtx());
    auto mockSource =
        DocumentSourceMock::createForTest({"{_id: 10, a: 15}", "{_id: 3, a: 5}"}, expCtx());
    pipeline->addInitialSource(mockSource.get());

    // We sample all of the documents since numSplitPoints(1) * samplingRatio (2) = 2 and the
    // document source has 2 chunks. So we can assert on the returned values.
    auto next = pipeline->getNext();
    ASSERT_EQUALS(next.get().getField("_id").getInt(), 3);
    ASSERT(next.get().getField("a").missing());
    next = pipeline->getNext();
    ASSERT_EQUALS(next.get().getField("_id").getInt(), 10);
    ASSERT(next.get().getField("a").missing());
    ASSERT(!pipeline->getNext());
}

TEST_F(ReshardingSplitPolicyTest, CompoundShardKeyWithNonDottedHashedFieldSucceeds) {
    auto shardKeyPattern = ShardKeyPattern(BSON("a" << 1 << "b"
                                                    << "hashed"));

    auto pipeline =
        Pipeline::parse(ReshardingSplitPolicy::createRawPipeline(
                            shardKeyPattern, 2 /* samplingRatio */, 1 /* numSplitPoints */),
                        expCtx());
    auto mockSource = DocumentSourceMock::createForTest(
        {"{x: 1, b: 16, a: 15}", "{x: 2, b: 123, a: 5}"}, expCtx());
    pipeline->addInitialSource(mockSource.get());

    // We sample all of the documents since numSplitPoints(1) * samplingRatio (2) = 2 and the
    // document source has 2 chunks. So we can assert on the returned values.
    auto next = pipeline->getNext();
    ASSERT_EQUALS(next.get().getField("a").getInt(), 5);
    ASSERT_EQUALS(next.get().getField("b").getLong(), -6548868637522515075LL);
    ASSERT(next.get().getField("x").missing());
    next = pipeline->getNext();
    ASSERT_EQUALS(next.get().getField("a").getInt(), 15);
    ASSERT_EQUALS(next.get().getField("b").getLong(), 2598032665634823220LL);
    ASSERT(next.get().getField("x").missing());
    ASSERT(!pipeline->getNext());
}

TEST_F(ReshardingSplitPolicyTest, CompoundShardKeyWithDottedFieldSucceeds) {
    auto shardKeyPattern = ShardKeyPattern(BSON("a.b" << 1 << "c" << 1));

    auto pipeline =
        Pipeline::parse(ReshardingSplitPolicy::createRawPipeline(
                            shardKeyPattern, 2 /* samplingRatio */, 1 /* numSplitPoints */),
                        expCtx());
    auto mockSource = DocumentSourceMock::createForTest(
        {"{x: 10, a: {b: 20}, c: 1}", "{x: 3, a: {b: 10}, c: 5}"}, expCtx());
    pipeline->addInitialSource(mockSource.get());

    // We sample all of the documents since numSplitPoints(1) * samplingRatio (2) = 2 and the
    // document source has 2 chunks. So we can assert on the returned values.
    auto next = pipeline->getNext();
    ASSERT_BSONOBJ_EQ(next.get().toBson(), BSON("a" << BSON("b" << 10) << "c" << 5));
    next = pipeline->getNext();
    ASSERT_BSONOBJ_EQ(next.get().toBson(), BSON("a" << BSON("b" << 20) << "c" << 1));
    ASSERT(!pipeline->getNext());
}

TEST_F(ReshardingSplitPolicyTest, CompoundShardKeyWithDottedHashedFieldSucceeds) {
    auto shardKeyPattern = ShardKeyPattern(BSON("a.b" << 1 << "c" << 1 << "a.c"
                                                      << "hashed"));

    auto pipeline =
        Pipeline::parse(ReshardingSplitPolicy::createRawPipeline(
                            shardKeyPattern, 2 /* samplingRatio */, 1 /* numSplitPoints */),
                        expCtx());
    auto mockSource = DocumentSourceMock::createForTest(
        {"{x: 10, a: {b: 20, c: 16}, c: 1}", "{x: 3, a: {b: 10, c: 123}, c: 5}"}, expCtx());
    pipeline->addInitialSource(mockSource.get());

    // We sample all of the documents since numSplitPoints(1) * samplingRatio (2) = 2 and the
    // document source has 2 chunks. So we can assert on the returned values.
    auto next = pipeline->getNext();
    ASSERT_BSONOBJ_EQ(next.get().toBson(),
                      BSON("a" << BSON("b" << 10 << "c" << -6548868637522515075LL) << "c" << 5));
    next = pipeline->getNext();
    ASSERT_BSONOBJ_EQ(next.get().toBson(),
                      BSON("a" << BSON("b" << 20 << "c" << 2598032665634823220LL) << "c" << 1));
    ASSERT(!pipeline->getNext());
}

TEST_F(ReshardingSplitPolicyTest, SamplingSuceeds) {
    auto shards = setupNShards(2);
    loadRoutingTableWithTwoChunksAndTwoShards(kTestAggregateNss);

    // We add a $sortKey field since AsyncResultsMerger expects it in order to merge the batches
    // from different shards.
    std::vector<BSONObj> firstShardChunks;
    for (int a = 0; a < 11; a++) {
        firstShardChunks.emplace_back(BSON("a" << a << "$sortKey" << BSON_ARRAY(a)));
    }

    std::vector<BSONObj> secondShardChunks;
    for (int a = 11; a < 30; a++) {
        secondShardChunks.emplace_back(BSON("a" << a << "$sortKey" << BSON_ARRAY(a)));
    }

    auto shardKeyPattern = ShardKeyPattern(BSON("a" << 1));
    std::vector<ShardId> shardIds;
    for (auto&& shard : shards) {
        shardIds.push_back(ShardId(shard.getName()));
    }

    auto future = launchAsync([&] {
        const int numInitialChunks = 4;
        const int samplingRatio = 10;
        auto rawPipeline = ReshardingSplitPolicy::createRawPipeline(
            shardKeyPattern, numInitialChunks - 1, samplingRatio);
        auto policy = ReshardingSplitPolicy(operationContext(),
                                            kTestAggregateNss,
                                            rawPipeline,
                                            numInitialChunks,
                                            shardIds,
                                            expCtx(),
                                            samplingRatio);
        const auto chunks = policy
                                .createFirstChunks(operationContext(),
                                                   shardKeyPattern,
                                                   {kTestAggregateNss, boost::none, primaryShardId})
                                .chunks;
        // We sample all of the documents since numSplitPoints(3) * samplingRatio (10) = 30 and the
        // document source has 23 chunks. So we can assert on the split points.
        ASSERT_EQ(chunks.size(), 4);
        ASSERT_BSONOBJ_EQ(chunks.at(0).getMin(), shardKeyPattern.getKeyPattern().globalMin());
        ASSERT_BSONOBJ_EQ(chunks.at(0).getMax(), firstShardChunks.at(0).removeField("$sortKey"));

        ASSERT_BSONOBJ_EQ(chunks.at(1).getMin(), firstShardChunks.at(0).removeField("$sortKey"));
        ASSERT_BSONOBJ_EQ(chunks.at(1).getMax(), firstShardChunks.at(10).removeField("$sortKey"));

        ASSERT_BSONOBJ_EQ(chunks.at(2).getMin(), firstShardChunks.at(10).removeField("$sortKey"));
        ASSERT_BSONOBJ_EQ(chunks.at(2).getMax(), secondShardChunks.at(9).removeField("$sortKey"));

        ASSERT_BSONOBJ_EQ(chunks.at(3).getMin(), secondShardChunks.at(9).removeField("$sortKey"));
        ASSERT_BSONOBJ_EQ(chunks.at(3).getMax(), shardKeyPattern.getKeyPattern().globalMax());
    });

    onCommand([&](const executor::RemoteCommandRequest& request) {
        return CursorResponse(kTestAggregateNss, CursorId{0}, firstShardChunks)
            .toBSON(CursorResponse::ResponseType::InitialResponse);
    });

    onCommand([&](const executor::RemoteCommandRequest& request) {
        return CursorResponse(kTestAggregateNss, CursorId{0}, secondShardChunks)
            .toBSON(CursorResponse::ResponseType::InitialResponse);
    });

    future.default_timed_get();
}

}  // namespace
}  // namespace mongo
