/**
 * Tests that DBClientRS performs re-targeting when it sees an ErrorCodes.NotMaster error response
 * from a command even if "not master" doesn't appear in the message.
 * @tags: [requires_replication]
 */
(function() {
"use strict";

// Set the refresh period to 10 min to rule out races
_setShellFailPoint({
    configureFailPoint: "modifyReplicaSetMonitorDefaultRefreshPeriod",
    mode: "alwaysOn",
    data: {
        period: 10 * 60,
    },
});

const rst = new ReplSetTest({
    nodes: 3,
    nodeOptions: {
        setParameter:
            {"failpoint.respondWithNotPrimaryInCommandDispatch": tojson({mode: "alwaysOn"})}
    }
});
rst.startSet();
rst.initiate();

const directConn = rst.getPrimary();
const rsConn = new Mongo(rst.getURL());
assert(rsConn.isReplicaSetConnection(),
       "expected " + rsConn.host + " to be a replica set connection string");

const [secondary1, secondary2] = rst.getSecondaries();

function stepDownPrimary(rst) {
    const awaitShell = startParallelShell(
        () => assert.commandWorked(db.adminCommand({replSetStepDown: 60, force: true})),
        directConn.port);

    // We wait for the primary to transition to the SECONDARY state to ensure we're waiting
    // until after the parallel shell has started the replSetStepDown command and the server is
    // paused at the failpoint.Do not attempt to reconnect to the node, since the node will be
    // holding the global X lock at the failpoint.
    const reconnectNode = false;
    rst.waitForState(directConn, ReplSetTest.State.SECONDARY, null, reconnectNode);

    return awaitShell;
}

const failpoint = "stepdownHangBeforePerformingPostMemberStateUpdateActions";
assert.commandWorked(directConn.adminCommand({configureFailPoint: failpoint, mode: "alwaysOn"}));

const awaitShell = stepDownPrimary(rst);

// Wait for a new primary to be elected and agreed upon by nodes.
rst.getPrimary();
rst.awaitNodesAgreeOnPrimary();

// DBClientRS should discover the current primary eventually and get NotMaster errors in the
// meantime.
assert.soon(() => {
    const res = rsConn.getDB("test").runCommand({create: "mycoll"});
    if (!res.ok) {
        assert(res.code == ErrorCodes.NotMaster)
    }
    return res.ok;
});

try {
    assert.commandWorked(directConn.adminCommand({configureFailPoint: failpoint, mode: "off"}));
} catch (e) {
    if (!isNetworkError(e)) {
        throw e;
    }

    // We ignore network errors because it's possible that depending on how quickly the server
    // closes connections that the connection would get closed before the server has a chance to
    // respond to the configureFailPoint command with ok=1.
}

awaitShell();

rst.stopSet();
})();
