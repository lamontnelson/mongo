/**
 * Tests that the time-tracking metrics in the 'transaction' object in currentOp() are being tracked
 * correctly.
 * @tags: [uses_transactions, uses_prepare_transaction]
 */

(function() {
'use strict';
load('jstests/sharding/libs/sharded_transactions_helpers.js');

function commitTxn(lsid, txnNumber) {
    const runCommitExpectSuccessCode = "assert.commandWorked(db.adminCommand({" +
        "commitTransaction: 1," +
        "lsid: " + tojson(lsid) + "," +
        "txnNumber: NumberLong(" + txnNumber + ")," +
        "stmtId: NumberInt(0)," +
        "autocommit: false," +
        "}));";
    return startParallelShell(runCommitExpectSuccessCode, st.s.port);
}

const st = new ShardingTest({shards: 2, config: 1});
const collName = 'currentop_two_phase';
const testDB = st.getDB('test');
const adminDB = st.getDB('admin');
const dbName = "test";
const ns = dbName + "." + collName;
const coordinator = st.shard0;
testDB.runCommand({drop: collName, writeConcern: {w: "majority"}});
assert.commandWorked(testDB[collName].insert({x: 1}, {writeConcern: {w: "majority"}}));

const session = adminDB.getMongo().startSession({causalConsistency: false});
const sessionDB = session.getDatabase('test');

assert.commandWorked(st.s.adminCommand({enableSharding: dbName}));
assert.commandWorked(st.s.adminCommand({movePrimary: dbName, to: coordinator.shardName}));
assert.commandWorked(st.s.adminCommand({shardCollection: ns, key: {_id: 1}}));
assert.commandWorked(st.s.adminCommand({split: ns, middle: {_id: 0}}));
assert.commandWorked(st.s.adminCommand({moveChunk: ns, find: {_id: 0}, to: st.shard1.shardName}));

assert.commandWorked(coordinator.adminCommand({_flushRoutingTableCacheUpdates: ns}));
assert.commandWorked(st.shard1.adminCommand({_flushRoutingTableCacheUpdates: ns}));

const failPoints =
    ['hangAfterSendingCoordinateCommitTransaction', 'hangBeforeWritingParticipantList'];

failPoints.forEach(function(fpName) {
    assert.commandWorked(coordinator.adminCommand({
        configureFailPoint: fpName,
        mode: "alwaysOn",
    }));
});

session.startTransaction();
// Run a few operations so that the transaction goes through several active/inactive periods.
assert.commandWorked(sessionDB[collName].insert({_id: -1}));
assert.commandWorked(sessionDB[collName].insert({_id: 1}));

print("XXX: before commit");
print("session: ", tojson(session.getSessionId()));
const commitJoin = commitTxn(session.getSessionId(), session.getTxnNumber_forTesting());

const sendCoordinatorCommitfilter = {
    active: true,
    "command.coordinateCommitTransaction": 1,
    'command.lsid.id': session.getSessionId().id,
    'command.txnNumber': NumberLong(0),
    'command.coordinator': true,
    'command.autocommit': false
};
// printjson(sendCoordinatorCommitfilter);

waitForFailpoint("Hit hangAfterSendingCoordinateCommitTransaction failpoint", 1);
var createCoordinateCommitTxnOp;
createCoordinateCommitTxnOp =
    adminDB.aggregate([{$currentOp: {}}, {$match: sendCoordinatorCommitfilter}]).toArray();
printjson(createCoordinateCommitTxnOp);

const writeParticipantFilter = {
    active: true,
    'twoPhaseCommitCoordinator.lsid.id': session.getSessionId().id,
    'twoPhaseCommitCoordinator.txnNumber': NumberLong(0),
    'twoPhaseCommitCoordinator.action': "writingParticipantsList",
    'twoPhaseCommitCoordinator.startTime': {$exists: true}
};
// printjson(writeParticipantFilter);

waitForFailpoint("Hit hangBeforeWritingParticipantList failpoint", 1);
print("XXX: writeParticipant");
var writeParticipantOp;
writeParticipantOp =
    adminDB.aggregate([{$currentOp: {}}, {$match: writeParticipantFilter}]).toArray();
printjson(writeParticipantOp);

print("XXX: UNFILTERED:");
printjson(adminDB.aggregate([{$currentOp: {}}, {$match: {}}]).toArray());

assert.eq(1, createCoordinateCommitTxnOp.length);
assert.eq(1, writeParticipantOp.length);

failPoints.forEach(function(fpName) {
    assert.commandWorked(coordinator.adminCommand({
        configureFailPoint: fpName,
        mode: "off",
    }));
});

commitJoin();

session.endSession();
st.stop();
})();
