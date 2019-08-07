/**
 * Tests that the transaction items in the 'twoPhaseCommitPhaseCommit' object in currentOp() are
 * being tracked correctly.
 * @tags: [uses_transactions, uses_prepare_transaction]
 */

(function() {
'use strict';
load('jstests/sharding/libs/sharded_transactions_helpers.js');

function commitTxn(st, lsid, txnNumber, assertWorked = true) {
    let cmd = "db.adminCommand({" +
        "commitTransaction: 1," +
        "lsid: " + tojson(lsid) + "," +
        "txnNumber: NumberLong(" + txnNumber + ")," +
        "stmtId: NumberInt(0)," +
        "autocommit: false," +
        "})";
    if (assertWorked) {
        cmd = "assert.commandWorked(" + cmd + ");";
    } else {
        cmd += ";";
    }
    return startParallelShell(cmd, st.s.port);
}

function afterFailpoint(fpName, fn = function() {}, fpCount = 1, disable = true) {
    const expectedLog = "Hit " + fpName + " failpoint";
    waitForFailpoint(expectedLog, fpCount);
    let result = fn();
    if (disable) {
        assert.commandWorked(coordinator.adminCommand({
            configureFailPoint: fpName,
            mode: "off",
        }));
    }
    return result;
}

function makeWorkerFilterWithAction(session, action) {
    return {
        active: true,
        'twoPhaseCommitCoordinator.lsid.id': session.getSessionId().id,
        'twoPhaseCommitCoordinator.txnNumber': NumberLong(0),
        'twoPhaseCommitCoordinator.action': action,
        'twoPhaseCommitCoordinator.startTime': {$exists: true}
    };
}

function setupDatabase(st, dbName, collectionName) {
    const ns = dbName + "." + collectionName;
    testDB.runCommand({drop: collectionName, writeConcern: {w: "majority"}});
    assert.commandWorked(testDB[collectionName].insert({x: 1}, {writeConcern: {w: "majority"}}));
    assert.commandWorked(st.s.adminCommand({enableSharding: dbName}));
    assert.commandWorked(st.s.adminCommand({movePrimary: dbName, to: coordinator.shardName}));
    assert.commandWorked(st.s.adminCommand({shardCollection: ns, key: {_id: 1}}));
    assert.commandWorked(st.s.adminCommand({split: ns, middle: {_id: 0}}));
    assert.commandWorked(
        st.s.adminCommand({moveChunk: ns, find: {_id: 0}, to: st.shard1.shardName}));
    assert.commandWorked(coordinator.adminCommand({_flushRoutingTableCacheUpdates: ns}));
    assert.commandWorked(st.shard1.adminCommand({_flushRoutingTableCacheUpdates: ns}));
}

function enableFailPoints(failPoints) {
    failPoints.forEach(function(fpName) {
        assert.commandWorked(coordinator.adminCommand({
            configureFailPoint: fpName,
            mode: "alwaysOn",
        }));
    });
}

function startTransaction(session, collectionName, insertValue = 1) {
    const dbName = session.getDatabase('test');
    session.startTransaction();
    // insert into both shards
    assert.commandWorked(dbName[collectionName].insert({_id: -1 * insertValue}));
    assert.commandWorked(dbName[collectionName].insert({_id: insertValue}));
}

// Setup test
const numShards = 2;
const st = new ShardingTest({shards: numShards, config: 1});
const dbName = "test";
const collectionName = 'currentop_two_phase';
const testDB = st.getDB('test');
const adminDB = st.getDB('admin');
const coordinator = st.shard0;
const participant = st.shard1;
const failPoints = [
    'hangAfterSendingCoordinateCommitTransaction',
    'hangBeforeWritingParticipantList',
    'hangBeforeSendingPrepare',
    'hangBeforeWritingDecision',
    'hangBeforeSendingCommit',
    'hangBeforeDeletingCoordinatorDoc',
    'hangBeforeSendingAbort'
];

var session = adminDB.getMongo().startSession({causalConsistency: false});

const sendCoordinatorCommitfilter = {
    active: true,
    'command.coordinateCommitTransaction': 1,
    'command.lsid.id': session.getSessionId().id,
    'command.txnNumber': NumberLong(0),
    'command.coordinator': true,
    'command.autocommit': false
};
const writeParticipantFilter = makeWorkerFilterWithAction(session, "writingParticipantList");
const sendPrepareFilter = makeWorkerFilterWithAction(session, "sendingPrepare");
const sendCommitFilter = makeWorkerFilterWithAction(session, "sendingCommit");
const writingDecisionFilter = makeWorkerFilterWithAction(session, "writingDecision");
const deletingCoordinatorFilter = makeWorkerFilterWithAction(session, "deletingCoordinatorDoc");

// Execute Test

// commit path
setupDatabase(st, dbName, collectionName);
enableFailPoints(failPoints);
startTransaction(session, collectionName);
let txnNumber = session.getTxnNumber_forTesting();
let lsid = session.getSessionId();
let commitJoin = commitTxn(st, lsid, txnNumber);

let createCoordinateCommitTxnOp =
    afterFailpoint("hangAfterSendingCoordinateCommitTransaction", function() {
        return adminDB.aggregate([{$currentOp: {}}, {$match: sendCoordinatorCommitfilter}])
            .toArray();
    });

let writeParticipantOp = afterFailpoint('hangBeforeWritingParticipantList', function() {
    return adminDB.aggregate([{$currentOp: {}}, {$match: writeParticipantFilter}]).toArray();
});

let sendPrepareOp = afterFailpoint('hangBeforeSendingPrepare', function() {
    return adminDB.aggregate([{$currentOp: {}}, {$match: sendPrepareFilter}]).toArray();
}, numShards);

let writeDecisionOp = afterFailpoint('hangBeforeWritingDecision', function() {
    return adminDB.aggregate([{$currentOp: {}}, {$match: writingDecisionFilter}]).toArray();
});

let sendCommitOp = afterFailpoint('hangBeforeSendingCommit', function() {
    return adminDB.aggregate([{$currentOp: {}}, {$match: sendCommitFilter}]).toArray();
}, numShards);

let deletingCoordinatorDocOp = afterFailpoint('hangBeforeDeletingCoordinatorDoc', function() {
    return adminDB.aggregate([{$currentOp: {}}, {$match: deletingCoordinatorFilter}]).toArray();
});

assert.eq(1, createCoordinateCommitTxnOp.length);
assert.eq(1, writeParticipantOp.length);
assert.eq(numShards, sendPrepareOp.length);
assert.eq(1, writeDecisionOp.length);
assert.eq(numShards, sendCommitOp.length);
assert.eq(1, deletingCoordinatorDocOp.length);

commitJoin();

// abort path
session = adminDB.getMongo().startSession({causalConsistency: false});
startTransaction(session, collectionName, 2);
txnNumber = session.getTxnNumber_forTesting();
lsid = session.getSessionId();
// Manually abort the transaction on one of the participants, so that the participant fails to
// prepare and failpoint is triggered on the coordinator.
assert.commandWorked(participant.adminCommand({
    abortTransaction: 1,
    lsid: lsid,
    txnNumber: NumberLong(txnNumber),
    stmtId: NumberInt(0),
    autocommit: false,
}));
commitJoin = commitTxn(st, lsid, txnNumber, false);
const sendAbortFilter = makeWorkerFilterWithAction(session, "sendingAbort");
let sendingAbortOp = afterFailpoint('hangBeforeSendingAbort', function() {
    return adminDB.aggregate([{$currentOp: {}}, {$match: sendAbortFilter}]).toArray();
}, numShards);

assert.eq(numShards, sendingAbortOp.length);

commitJoin();

session.endSession();
st.stop();
})();
