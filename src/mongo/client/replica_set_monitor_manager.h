/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#pragma once

#include <string>
#include <vector>

#include "mongo/client/replica_set_change_notifier.h"
#include "mongo/executor/task_executor.h"
#include "mongo/platform/mutex.h"
#include "mongo/util/string_map.h"

namespace mongo {

class BSONObjBuilder;
class ConnectionString;
class ReplicaSetMonitor;
class MongoURI;

/**
 * Manages the lifetime of a set of replica set monitors.
 */
class ReplicaSetMonitorManager {
public:
    /**
     * Create or retrieve a monitor for a particular replica set. The getter method returns
     * nullptr if there is no monitor registered for the particular replica set.
     */
    virtual std::shared_ptr<ReplicaSetMonitor> getMonitor(StringData setName) = 0;
    virtual std::shared_ptr<ReplicaSetMonitor> getOrCreateMonitor(const ConnectionString& connStr) = 0;
    virtual std::shared_ptr<ReplicaSetMonitor> getOrCreateMonitor(const MongoURI& uri) = 0;

    /**
     * Retrieves the names of all sets tracked by this manager.
     */
    virtual std::vector<std::string> getAllSetNames() = 0;

    /**
     * Removes the specified replica set monitor from being tracked, if it exists. Otherwise
     * does nothing. Once all shared_ptr references to that monitor are released, the monitor
     * will be destroyed and will no longer be tracked.
     */
    virtual void removeMonitor(StringData setName) = 0;

    /**
     * Removes and destroys all replica set monitors. Should be used for unit tests only.
     */
    virtual void removeAllMonitors() = 0;

    /**
     * Shuts down _taskExecutor.
     */
    virtual void shutdown() = 0;

    /**
     * Reports information about the replica sets tracked by us, for diagnostic purposes. If
     * forFTDC, trim to minimize its size for full-time diagnostic data capture.
     */
    virtual void report(BSONObjBuilder* builder, bool forFTDC = false) = 0;

    /**
     * Returns an executor for running RSM tasks.
     */
    virtual executor::TaskExecutor* getExecutor() = 0;

    virtual ReplicaSetChangeNotifier& getNotifier() = 0;

    virtual bool isShutdown() const = 0;
};

class ReplicaSetMonitorManagerImpl : public ReplicaSetMonitorManager {
    ReplicaSetMonitorManagerImpl(const ReplicaSetMonitorManagerImpl&) = delete;
    ReplicaSetMonitorManagerImpl& operator=(const ReplicaSetMonitorManagerImpl&) = delete;

public:
    ReplicaSetMonitorManagerImpl();
    virtual ~ReplicaSetMonitorManagerImpl();

    /**
     * Create or retrieve a monitor for a particular replica set. The getter method returns
     * nullptr if there is no monitor registered for the particular replica set.
     */
    std::shared_ptr<ReplicaSetMonitor> getMonitor(StringData setName);
    std::shared_ptr<ReplicaSetMonitor> getOrCreateMonitor(const ConnectionString& connStr);
    std::shared_ptr<ReplicaSetMonitor> getOrCreateMonitor(const MongoURI& uri);

    /**
     * Retrieves the names of all sets tracked by this manager.
     */
    std::vector<std::string> getAllSetNames();

    /**
     * Removes the specified replica set monitor from being tracked, if it exists. Otherwise
     * does nothing. Once all shared_ptr references to that monitor are released, the monitor
     * will be destroyed and will no longer be tracked.
     */
    void removeMonitor(StringData setName);

    /**
     * Removes and destroys all replica set monitors. Should be used for unit tests only.
     */
    void removeAllMonitors();

    /**
     * Shuts down _taskExecutor.
     */
    void shutdown();

    /**
     * Reports information about the replica sets tracked by us, for diagnostic purposes. If
     * forFTDC, trim to minimize its size for full-time diagnostic data capture.
     */
    void report(BSONObjBuilder* builder, bool forFTDC = false);

    /**
     * Returns an executor for running RSM tasks.
     */
    executor::TaskExecutor* getExecutor();

    ReplicaSetChangeNotifier& getNotifier();

    bool isShutdown() const;

private:
    using ReplicaSetMonitorsMap = StringMap<std::weak_ptr<ReplicaSetMonitor>>;

    // Protects access to the replica set monitors
    mutable Mutex _mutex = MONGO_MAKE_LATCH("ReplicaSetMonitorManager::_mutex");

    // Executor for monitoring replica sets.
    std::unique_ptr<executor::TaskExecutor> _taskExecutor;

    // Widget to notify listeners when a RSM notices a change
    ReplicaSetChangeNotifier _notifier;

    // Needs to be after `_taskExecutor`, so that it will be destroyed before the `_taskExecutor`.
    ReplicaSetMonitorsMap _monitors;

    void _setupTaskExecutorInLock();

    // set to true when shutdown has been called.
    bool _isShutdown{false};
};

}  // namespace mongo
