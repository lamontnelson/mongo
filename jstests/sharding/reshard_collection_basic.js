//
// Basic tests for reshardCollection.
// @tags: [
//   requires_fcv_47,
//   uses_atclustertime,
// ]
//

load("jstests/libs/uuid_util.js");

(function() {
'use strict';

const numShards = 3;
const st = new ShardingTest({mongos: 1, shards: numShards});
const kDbName = 'db';
const collName = 'foo';
const ns = kDbName + '.' + collName;
const numCollDocs = 1000;
const numCollChunks = 5;
const mongos = st.s0;
const config = st.config0;
const mongosConfig = mongos.getDB('config');
const db = mongos.getDB(kDbName);
const existingZoneName = 'x1';

let shardToRSMap = {};
Array.from(Array(numShards).keys()).forEach(i => {
    shardToRSMap[st['shard' + i].shardName] = st['rs' + i];
});

let getUUIDFromCollectionInfo = (dbName, collName, collInfo) => {
    if (collInfo) {
        return extractUUIDFromObject(collInfo.info.uuid);
    }

    const uuidObject = getUUIDFromListCollections(mongos.getDB(dbName), collName);
    return extractUUIDFromObject(uuidObject);
};

let constructTemporaryReshardingCollName = (dbName, collName, collInfo) => {
    const existingUUID = getUUIDFromCollectionInfo(dbName, collName, collInfo);
    return 'system.resharding.' + existingUUID;
};

let getAllShardIdsFromExpectedChunks = (expectedChunks) => {
    let shardIds = new Set();
    expectedChunks.forEach(chunk => {
        shardIds.add(chunk.recipientShardId);
    });
    return shardIds;
};

let verifyTemporaryReshardingChunksMatchExpected = (numExpectedChunks, presetExpectedChunks) => {
    const tempReshardingCollNs =
        kDbName + '.' + constructTemporaryReshardingCollName(kDbName, collName);
    const tempReshardingChunks = mongosConfig.chunks.find({ns: tempReshardingCollNs}).toArray();

    if (presetExpectedChunks) {
        presetExpectedChunks.sort();
    }

    tempReshardingChunks.sort();
    assert.eq(numExpectedChunks, tempReshardingChunks.length);

    let shardChunkCounts = {};
    let incChunkCount = key => {
        if (shardChunkCounts.hasOwnProperty(key)) {
            shardChunkCounts[key]++;
        } else {
            shardChunkCounts[key] = 1;
        }
    };

    for (let i = 0; i < numExpectedChunks; i++) {
        incChunkCount(tempReshardingChunks[i].shard);

        // match exact chunk boundaries for presetExpectedChunks
        if (presetExpectedChunks) {
            assert.eq(presetExpectedChunks[i].recipientShardId, tempReshardingChunks[i].shard);
            assert.eq(presetExpectedChunks[i].min, tempReshardingChunks[i].min);
            assert.eq(presetExpectedChunks[i].max, tempReshardingChunks[i].max);
        }
    }

    // if presetChunks not specified, we only assert that chunks counts are balanced across shards
    if (!presetExpectedChunks) {
        let maxDiff = 0;
        let shards = Object.keys(shardChunkCounts);
        shards.forEach(shard1 => {
            shards.forEach(shard2 => {
                let diff = Math.abs(shardChunkCounts[shard1] - shardChunkCounts[shard2]);
                maxDiff = (diff > maxDiff) ? diff : maxDiff;
            });
        });
        assert.lte(maxDiff, 1);
    }
};

let verifyTemporaryReshardingCollectionExistsWithCorrectOptionsForConn =
    (expectedCollInfo, tempCollName, conn) => {
        const tempReshardingCollInfo =
            conn.getDB(kDbName).getCollectionInfos({name: tempCollName})[0];
        assert.neq(tempReshardingCollInfo, null);
        assert.eq(expectedCollInfo.options, tempReshardingCollInfo.options);
    };

let verifyTemporaryReshardingCollectionExistsWithCorrectOptions = (expectedRecipientShards) => {
    const originalCollInfo = mongos.getDB(kDbName).getCollectionInfos({name: collName})[0];
    assert.neq(originalCollInfo, null);

    const tempReshardingCollName =
        constructTemporaryReshardingCollName(kDbName, collName, originalCollInfo);

    verifyTemporaryReshardingCollectionExistsWithCorrectOptionsForConn(
        originalCollInfo, tempReshardingCollName, mongos);

    expectedRecipientShards.forEach(shardId => {
        verifyTemporaryReshardingCollectionExistsWithCorrectOptionsForConn(
            originalCollInfo, tempReshardingCollName, shardToRSMap[shardId].getPrimary());
    });
};

let removeAllReshardingCollections = () => {
    const tempReshardingCollName = constructTemporaryReshardingCollName(kDbName, collName);
    mongos.getDB(kDbName).getCollection(collName).drop();
    mongos.getDB(kDbName).getCollection(tempReshardingCollName).drop();
    mongosConfig.chunks.remove({ns: ns});
    mongosConfig.reshardingOperations.remove({nss: ns});
    mongosConfig.collections.remove({reshardingFields: {$exists: true}});

    st.rs0.getPrimary().getDB('config').localReshardingOperations.donor.remove({nss: ns});
    st.rs0.getPrimary().getDB('config').localReshardingOperations.recipient.remove({nss: ns});
    st.rs1.getPrimary().getDB('config').localReshardingOperations.donor.remove({nss: ns});
    st.rs1.getPrimary().getDB('config').localReshardingOperations.recipient.remove({nss: ns});
};

let assertSuccessfulReshardCollection = (commandObj, presetReshardedChunks) => {
    let numInitialChunks = commandObj['numInitialChunks'] || numShards;

    if (presetReshardedChunks)
        commandObj._presetReshardedChunks = presetReshardedChunks;
    assert.commandWorked(mongos.adminCommand(commandObj));

    if (presetReshardedChunks) {
        verifyTemporaryReshardingChunksMatchExpected(presetReshardedChunks.length,
                                                     presetReshardedChunks);
        verifyTemporaryReshardingCollectionExistsWithCorrectOptions(
            getAllShardIdsFromExpectedChunks(presetReshardedChunks));
    } else {
        const configChunksArray = mongosConfig.chunks.find({'ns': ns});
        verifyTemporaryReshardingChunksMatchExpected(numInitialChunks);
    }

    resetPersistedData();
};

let presetReshardedChunks =
    [{recipientShardId: st.shard1.shardName, min: {_id: MinKey}, max: {_id: MaxKey}}];

let resetPersistedData =
    () => {
        removeAllReshardingCollections();
        insertData(numCollDocs);
        shardCollection(ns, numCollChunks, numCollDocs);
    }

let shardCollection = (ns, numChunks, numCollDocs) => {
    numChunks = numChunks || 1;
    numCollDocs = numCollDocs || 1000;

    assert.commandWorked(mongos.adminCommand({shardCollection: ns, key: {_id: 1}}));

    let cur = 0;
    Array.from(Array(numChunks - 1).keys()).forEach(i => {
        assert.commandWorked(mongos.adminCommand({split: ns, middle: {_id: cur}}));
        cur += (numCollDocs / numCollChunks);
    });

    Object.values(shardToRSMap).forEach(rs => {
        rs.awaitReplication();
    });
};

let insertData = (count) => {
    let bulk = db.getCollection(collName).initializeOrderedBulkOp();
    Array.from(Array(count).keys()).forEach(i => {
        bulk.insert({_id: i});
    });
    assert.commandWorked(bulk.execute());
};

/**
 * Fail cases
 */

jsTest.log("Fail if sharding is disabled.");
assert.commandFailedWithCode(mongos.adminCommand({reshardCollection: ns, key: {_id: 1}}),
                             ErrorCodes.NamespaceNotFound);

assert.commandWorked(mongos.adminCommand({enableSharding: kDbName}));

jsTest.log("Fail if collection is unsharded.");
assert.commandFailedWithCode(mongos.adminCommand({reshardCollection: ns, key: {_id: 1}}),
                             ErrorCodes.NamespaceNotSharded);

assert.commandWorked(mongos.adminCommand({shardCollection: ns, key: {_id: 1}}));

resetPersistedData();

jsTest.log("Fail if missing required key.");
assert.commandFailedWithCode(mongos.adminCommand({reshardCollection: ns}), 40414);

jsTest.log("Fail if unique is specified and is true.");
assert.commandFailedWithCode(
    mongos.adminCommand({reshardCollection: ns, key: {_id: 1}, unique: true}), ErrorCodes.BadValue);

jsTest.log("Fail if collation is specified and is not {locale: 'simple'}.");
assert.commandFailedWithCode(
    mongos.adminCommand({reshardCollection: ns, key: {_id: 1}, collation: {locale: 'en_US'}}),
    ErrorCodes.BadValue);

jsTest.log("Fail if both numInitialChunks and _presetReshardedChunks are provided.");
assert.commandFailedWithCode(mongos.adminCommand({
    reshardCollection: ns,
    key: {_id: 1},
    unique: false,
    collation: {locale: 'simple'},
    numInitialChunks: 2,
    _presetReshardedChunks: presetReshardedChunks
}),
                             ErrorCodes.BadValue);

jsTest.log(
    "Fail if authoritative tags exist in config.tags collection and zones are not provided.");
assert.commandWorked(
    st.s.adminCommand({addShardToZone: st.shard1.shardName, zone: existingZoneName}));
assert.commandWorked(st.s.adminCommand(
    {updateZoneKeyRange: ns, min: {_id: 0}, max: {_id: 5}, zone: existingZoneName}));

assert.commandFailedWithCode(mongos.adminCommand({
    reshardCollection: ns,
    key: {_id: 1},
    unique: false,
    collation: {locale: 'simple'},
    numInitialChunks: 2,
}),
                             ErrorCodes.BadValue);

jsTest.log(
    "Fail if authoritative tags exist in config.tags collection and zones are provided and use a name which does not exist in authoritative tags.");
assert.commandFailedWithCode(mongos.adminCommand({
    reshardCollection: ns,
    key: {_id: 1},
    unique: false,
    collation: {locale: 'simple'},
    zones: [{tag: 'x', min: {_id: 5}, max: {_id: 10}, ns: ns}],
    numInitialChunks: 2,
}),
                             ErrorCodes.BadValue);

/**
 * Success cases
 */
resetPersistedData();

jsTest.log("Succeed when correct locale is provided.");
assertSuccessfulReshardCollection(
    {reshardCollection: ns, key: {_id: 1}, collation: {locale: 'simple'}});

jsTest.log("Succeed base case.");
assertSuccessfulReshardCollection({reshardCollection: ns, key: {_id: 1}});

jsTest.log("Succeed if unique is specified and is false.");
assertSuccessfulReshardCollection({reshardCollection: ns, key: {_id: 1}, unique: false});

jsTest.log(
    "Succeed if _presetReshardedChunks is provided and test commands are enabled (default).");
assertSuccessfulReshardCollection({reshardCollection: ns, key: {_id: 1}}, presetReshardedChunks);

presetReshardedChunks = [
    {recipientShardId: st.shard0.shardName, min: {_id: MinKey}, max: {_id: 0}},
    {recipientShardId: st.shard1.shardName, min: {_id: 0}, max: {_id: MaxKey}}
];

jsTest.log("Succeed if all optional fields and numInitialChunks are provided with correct values.");
assertSuccessfulReshardCollection({
    reshardCollection: ns,
    key: {_id: 1},
    unique: false,
    collation: {locale: 'simple'},
    numInitialChunks: 5,
});

jsTest.log(
    "Succeed if all optional fields and _presetReshardedChunks are provided with correct values and test commands are enabled (default).");
assertSuccessfulReshardCollection(
    {reshardCollection: ns, key: {_id: 1}, unique: false, collation: {locale: 'simple'}},
    presetReshardedChunks);

jsTest.log(
    "Succeed if authoritative tags exist in config.tags collection and zones are provided and use an existing zone's name.");
assertSuccessfulReshardCollection({
    reshardCollection: ns,
    key: {_id: 1},
    unique: false,
    collation: {locale: 'simple'},
    zones: [{tag: existingZoneName, min: {_id: 5}, max: {_id: 10}, ns: ns}]
},
                                  presetReshardedChunks);

st.stop();
})();
