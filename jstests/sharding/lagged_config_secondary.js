/**
 * Test that mongos times out when the config server replica set only contains nodes that
 * are behind the majority opTime.
 */

// Checking UUID and index consistency involves mongos being able to do a read from the config
// server, but this test is designed to make mongos time out when reading from the config server.
TestData.skipCheckingUUIDsConsistentAcrossCluster = true;
TestData.skipCheckingIndexesConsistentAcrossCluster = true;

(function () {
    var st =
        new ShardingTest({shards: 1, configReplSetTestOptions: {settings: {chainingAllowed: false}}});
    var testDB = st.s.getDB('test');

    assert.commandWorked(testDB.adminCommand({enableSharding: 'test'}));
    assert.commandWorked(testDB.adminCommand({shardCollection: 'test.user', key: {_id: 1}}));

// Ensures that all metadata writes thus far have been replicated to all nodes
    st.configRS.awaitReplication();

    var configSecondaryList = st.configRS.getSecondaries();
    var configSecondaryToKill = configSecondaryList[0];
    var delayedConfigSecondary = configSecondaryList[1];

    assert.commandWorked(testDB.user.insert({_id: 1}));

    delayedConfigSecondary.getDB('admin').adminCommand(
        {configureFailPoint: 'rsSyncApplyStop', mode: 'alwaysOn'});

// Do one metadata write in order to bump the optime on mongos
    assert.commandWorked(st.getDB('config').TestConfigColl.insert({TestKey: 'Test value'}));

    st.configRS.stopMaster();
    MongoRunner.stopMongod(configSecondaryToKill);

// Clears all cached info so mongos will be forced to query from the config.
    st.s.adminCommand({flushRouterConfig: 1});

    print('Attempting read on a sharded collection...');
    var exception = assert.throws(function () {
        testDB.user.find({}).maxTimeMS(15000).itcount();
    });

    assert(ErrorCodes.isExceededTimeLimitError(exception.code));

// Can't do clean shutdown with this failpoint on.
    delayedConfigSecondary.getDB('admin').adminCommand(
        {configureFailPoint: 'rsSyncApplyStop', mode: 'off'});

    st.stop();
}());
