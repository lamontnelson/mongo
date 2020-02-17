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

#include <functional>
#include <memory>
#include <mongo/executor/task_executor.h>
#include <set>
#include <string>

#include "mongo/base/string_data.h"
#include "mongo/client/mongo_uri.h"
#include "mongo/client/replica_set_change_notifier.h"
#include "mongo/client/sdam/sdam.h"
#include "mongo/client/server_is_master_monitor.h"
#include "mongo/executor/task_executor.h"
#include "mongo/logger/log_component.h"
#include "mongo/util/concurrency/with_lock.h"
#include "mongo/util/duration.h"
#include "mongo/util/net/hostandport.h"
#include "mongo/util/time_support.h"

namespace mongo {

class BSONObj;
class ReplicaSetMonitor;
class ReplicaSetMonitorQueryProcessor;
class ReplicaSetMonitorTest;
struct ReadPreferenceSetting;
using ReplicaSetMonitorPtr = std::shared_ptr<ReplicaSetMonitor>;
using ReplicaSetMontiorQueryProcessorPtr = std::shared_ptr<ReplicaSetMonitorQueryProcessor>;
using ReplicaSetMonitorTask = std::function<void(ReplicaSetMonitorPtr)>;

/**
 * Holds state about a replica set and provides a means to refresh the local view.
 * All methods perform the required synchronization to allow callers from multiple threads.
 */
class ReplicaSetMonitor : public sdam::TopologyListener,
                          public std::enable_shared_from_this<ReplicaSetMonitor> {

    ReplicaSetMonitor(const ReplicaSetMonitor&) = delete;
    ReplicaSetMonitor& operator=(const ReplicaSetMonitor&) = delete;

public:
    class Refresher;

    static constexpr auto kExpeditedRefreshPeriod = Milliseconds(500);
    static constexpr auto kCheckTimeout = Seconds(5);

    /**
     * Constructs a RSM instance.
     */
    ReplicaSetMonitor(const MongoURI& uri,
                      std::shared_ptr<executor::TaskExecutor> executor = nullptr);

    /**
     * Performs post-construction initialization. This function must be called exactly once before
     * the instance is used. Call the make static function to create and init RSM instances.
     */
    void init();

    /**
     * Closes this RSM instance. This RSM instance is no longer useable once the function exits.
     */
    void close();

    /**
     * Create a Replica Set monitor instance and fully initialize it.
     * Use this instead of the constructor to create instances.
     */
    static ReplicaSetMonitorPtr make(const MongoURI& uri,
                                     std::shared_ptr<executor::TaskExecutor> executor = nullptr);

    /**
     * Permanently stops all monitoring on replica sets and clears all cached information
     * as well. As a consequence, NEVER call this if you have other threads that have a
     * DBClientReplicaSet instance. This method should be used for unit test only.
     */
    static void cleanup();

    /**
     * Returns a host matching the given read preference or an error, if no host matches.
     *
     * @param readPref Read preference to match against
     * @param maxWait If no host is readily available, which matches the specified read preference,
     *   wait for one to become available for up to the specified time and periodically refresh
     *   the view of the set. The call may return with an error earlier than the specified value,
     *   if none of the known hosts for the set are reachable within some number of attempts.
     *   Note that if a maxWait of 0ms is specified, this method may still attempt to contact
     *   every host in the replica set up to one time.
     *
     * Known errors are:
     *  FailedToSatisfyReadPreference, if node cannot be found, which matches the read preference.
     */
    SemiFuture<HostAndPort> getHostOrRefresh(const ReadPreferenceSetting& readPref,
                                             Milliseconds maxWait = kDefaultFindHostTimeout);

    SemiFuture<std::vector<HostAndPort>> getHostsOrRefresh(
        const ReadPreferenceSetting& readPref, Milliseconds maxWait = kDefaultFindHostTimeout);

    /**
     * Returns the host we think is the current master or uasserts.
     *
     * This is a thin wrapper around getHostOrRefresh so this will also refresh our view if we
     * don't think there is a master at first. The main difference is that this will uassert
     * rather than returning an empty HostAndPort.
     */
    HostAndPort getMasterOrUassert();

    /**
     * Notifies this Monitor that a host has failed because of the specified error 'status' and
     * should be considered down.
     */
    void failedHost(const HostAndPort& host, const Status& status);
    void failedHost(const HostAndPort& host, BSONObj bson, const Status& status);

    /**
     * Returns true if this node is the master based ONLY on local data. Be careful, return may
     * be stale.
     */
    bool isPrimary(const HostAndPort& host) const;

    /**
     * Returns true if host is part of this set and is considered up (meaning it can accept
     * queries).
     */
    bool isHostUp(const HostAndPort& host) const;

    /**
     * Returns the minimum wire version supported across the replica set.
     */
    int getMinWireVersion() const;

    /**
     * Returns the maximum wire version supported across the replica set.
     */
    int getMaxWireVersion() const;

    /**
     * The name of the set.
     */
    std::string getName() const;

    /**
     * Returns a std::string with the format name/server1,server2.
     * If name is empty, returns just comma-separated list of servers.
     * It IS updated to reflect the current members of the set.
     */
    std::string getServerAddress() const;

    /**
     * Returns the URI that was used to construct this monitor.
     * It IS NOT updated to reflect the current members of the set.
     */
    const MongoURI& getOriginalUri() const;

    /**
     * Is server part of this set? Uses only cached information.
     */
    bool contains(const HostAndPort& server) const;

    /**
     * Writes information about our cached view of the set to a BSONObjBuilder. If
     * forFTDC, trim to minimize its size for full-time diagnostic data capture.
     */
    void appendInfo(BSONObjBuilder& b, bool forFTDC = false) const;

    /**
     * Returns true if the monitor knows a usable primary from it's interal view.
     */
    bool isKnownToHaveGoodPrimary() const;

    /**
     * Creates a new ReplicaSetMonitor, if it doesn't already exist.
     */
    static std::shared_ptr<ReplicaSetMonitor> createIfNeeded(const std::string& name,
                                                             const std::set<HostAndPort>& servers);

    static std::shared_ptr<ReplicaSetMonitor> createIfNeeded(const MongoURI& uri);

    /**
     * gets a cached Monitor per name. If the monitor is not found and createFromSeed is false,
     * it will return none. If createFromSeed is true, it will try to look up the last known
     * servers list for this set and will create a new monitor using that as the seed list.
     */
    static std::shared_ptr<ReplicaSetMonitor> get(const std::string& name);

    /**
     * Removes the ReplicaSetMonitor for the given set name from _sets, which will delete it.
     * If clearSeedCache is true, then the cached seed std::string for this Replica Set will be
     * removed from _seedServers.
     */
    static void remove(const std::string& name);

    /**
     * Returns the change notifier for the underlying ReplicaMonitorManager
     */
    static ReplicaSetChangeNotifier& getNotifier();

    /**
     * Permanently stops all monitoring on replica sets.
     */
    static void shutdown();

    /**
     * The default timeout, which will be used for finding a replica set host if the caller does
     * not explicitly specify it.
     */
    static inline const Seconds kDefaultFindHostTimeout{15};

private:
    struct HostQuery {
        Date_t deadline;
        executor::TaskExecutor::CallbackHandle deadlineHandle;
        ReadPreferenceSetting criteria;
        Date_t start = Date_t::now();
        bool done = false;
        Promise<std::vector<HostAndPort>> promise;
    };
    using HostQueryPtr = std::shared_ptr<HostQuery>;

    SemiFuture<std::vector<HostAndPort>> _enqueueOutstandingQuery(
        WithLock, const ReadPreferenceSetting& criteria, const Date_t& deadline);

    std::vector<HostAndPort> _extractHosts(
        const std::vector<sdam::ServerDescriptionPtr>& serverDescriptions);
    boost::optional<std::vector<HostAndPort>> _getHosts(const TopologyDescriptionPtr& topology,
                                                        const ReadPreferenceSetting& criteria);
    boost::optional<std::vector<HostAndPort>> _getHosts(const ReadPreferenceSetting& criteria);

    void onTopologyDescriptionChangedEvent(UUID topologyId,
                                           sdam::TopologyDescriptionPtr previousDescription,
                                           sdam::TopologyDescriptionPtr newDescription) override;

    void onServerHeartbeatSucceededEvent(sdam::IsMasterRTT durationMs,
                                         const sdam::ServerAddress& hostAndPort,
                                         const BSONObj reply) override;

    void onServerHeartbeatFailureEvent(IsMasterRTT durationMs, Status errorStatus, const ServerAddress &hostAndPort,
                                       const BSONObj reply) override;

    void onServerPingFailedEvent(const sdam::ServerAddress& hostAndPort,
                                 const Status& status) override;

    void onServerPingSucceededEvent(sdam::IsMasterRTT durationMS,
                                    const sdam::ServerAddress& hostAndPort) override;

    // Get a pointer to the current primary's ServerDescription
    // To ensure a consistent view of the Topology either _currentPrimary or _currentTopology should
    // be called (not both) since the topology can change between the function invocations.
    boost::optional<sdam::ServerDescriptionPtr> _currentPrimary() const;

    // Get the current TopologyDescription
    // Note that most functions will want to save the result of this function once per computation
    // so that we are operating on a consistent read-only view of the topology.
    sdam::TopologyDescriptionPtr _currentTopology() const;

    std::string _logPrefix();

    void _failOutstandingWitStatus(WithLock, Status status);
    bool _hasMembershipChange(sdam::TopologyDescriptionPtr oldDescription,
                              sdam::TopologyDescriptionPtr newDescription);

    Status _makeUnsatisfiedReadPrefError(const ReadPreferenceSetting& criteria) const;
    Status _makeReplicaSetMonitorRemovedError() const;

    // execute the provided function with the RSM's mutex locked
    void _withLock(ReplicaSetMonitorTask f);

    sdam::SdamConfiguration _sdamConfig;
    sdam::TopologyManagerPtr _topologyManager;
    sdam::ServerSelectorPtr _serverSelector;
    sdam::TopologyEventsPublisherPtr _eventsPublisher;
    ServerIsMasterMonitorPtr _isMasterMonitor;

    const MongoURI _uri;

    std::shared_ptr<executor::TaskExecutor> _executor;
    ReplicaSetMontiorQueryProcessorPtr _queryProcessor;
    AtomicWord<bool> _isClosed{true};

    mutable Mutex _mutex = MONGO_MAKE_LATCH("ReplicaSetMonitor");
    ClockSource* _clockSource;
    std::vector<HostQueryPtr> _outstandingQueries;
    mutable PseudoRandom _random;

    static inline const auto kServerSelectionConfig =
        sdam::ServerSelectionConfiguration::defaultConfiguration();

    static inline const auto kLogPrefix = "[ReplicaSetMonitor]";
    static inline const auto kDefaultLogLevel = logger::LogSeverity::Debug(1);
    static inline const auto kLowerLogLevel = kDefaultLogLevel.lessSevere();

    friend class ReplicaSetMonitorQueryProcessor;
};

}  // namespace mongo
