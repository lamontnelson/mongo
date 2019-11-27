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
#pragma once
#include <memory>

#include "mongo/client/replica_set_monitor.h"
#include "mongo/client/sdam/sdam_datatypes.h"
#include "mongo/client/sdam/topology_description.h"
#include "mongo/client/sdam/topology_state_machine.h"

namespace mongo::sdam {
/**
 * This class serves as the public interface to the functionality described in the Service Discovery
 * and Monitoring spec:
 *   https://github.com/mongodb/specifications/blob/master/source/server-discovery-and-monitoring/server-discovery-and-monitoring.rst
 */
class TopologyManager : public mongo::ReplicaSetMonitor {
    TopologyManager() = delete;
    TopologyManager(const TopologyManager&) = delete;

public:
    TopologyManager(SdamConfiguration config, ClockSource* clockSource);

    /**
     * This function atomically:
     *   1. Clones the current TopologyDescription
     *   2. Executes the state machine logic given the cloned TopologyDescription and provided
     * IsMasterOutcome (containing the new ServerDescription).
     *   3. Installs the cloned (and possibly modified) TopologyDescription as the current one.
     *
     * Multiple threads may call this function concurrently. However, the manager will process the
     * IsMasterOutcomes serially, as required by:
     *   https://github.com/mongodb/specifications/blob/master/source/server-discovery-and-monitoring/server-discovery-and-monitoring.rst#process-one-ismaster-outcome-at-a-time
     */
    void onServerDescription(const IsMasterOutcome& isMasterOutcome);

    /**
     * Get the current TopologyDescription. This is safe to call from multiple threads.
     */
    const TopologyDescriptionPtr getTopologyDescription() const;

    // Replica Set Monitor Interface

    /**
     * Schedules the initial refresh task into task executor.
     */
    virtual void init() override;

    /**
     * Ends any ongoing refreshes.
     */
    virtual void drop() override;

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
    virtual SemiFuture<HostAndPort> getHostOrRefresh(
        const ReadPreferenceSetting& readPref, Milliseconds maxWait = kDefaultFindHostTimeout) override;

    virtual SemiFuture<std::vector<HostAndPort>> getHostsOrRefresh(
        const ReadPreferenceSetting& readPref, Milliseconds maxWait = kDefaultFindHostTimeout) override;

    /**
     * Returns the host we think is the current master or uasserts.
     *
     * This is a thin wrapper around getHostOrRefresh so this will also refresh our view if we
     * don't think there is a master at first. The main difference is that this will uassert
     * rather than returning an empty HostAndPort.
     */
    virtual HostAndPort getMasterOrUassert() override;

    /**
     * Notifies this Monitor that a host has failed because of the specified error 'status' and
     * should be considered down.
     *
     * Call this when you get a connection error. If you get an error while trying to refresh our
     * view of a host, call Refresher::failedHost instead because it bypasses taking the monitor's
     * mutex.
     */
    virtual void failedHost(const HostAndPort& host, const Status& status) override;

    /**
     * Returns true if this node is the master based ONLY on local data. Be careful, return may
     * be stale.
     */
    virtual bool isPrimary(const HostAndPort& host) const override;

    /**
     * Returns true if host is part of this set and is considered up (meaning it can accept
     * queries).
     */
    virtual bool isHostUp(const HostAndPort& host) const override;

    /**
     * Returns the minimum wire version supported across the replica set.
     */
    virtual int getMinWireVersion() const override;

    /**
     * Returns the maximum wire version supported across the replica set.
     */
    virtual int getMaxWireVersion() const override;

    /**
     * The name of the set.
     */
    virtual std::string getName() const override;

    /**
     * Returns a std::string with the format name/server1,server2.
     * If name is empty, returns just comma-separated list of servers.
     * It IS updated to reflect the current members of the set.
     */
    virtual std::string getServerAddress() const override;

    /**
     * Is server part of this set? Uses only cached information.
     */
    virtual bool contains(const HostAndPort& server) const override;

    /**
     * Writes information about our cached view of the set to a BSONObjBuilder. If
     * forFTDC, trim to minimize its size for full-time diagnostic data capture.
     */
    virtual void appendInfo(BSONObjBuilder& b, bool forFTDC = false) const override;

    /**
     * Returns true if the monitor knows a usable primary from it's interal view.
     */
    virtual bool isKnownToHaveGoodPrimary() const override;

private:
    boost::optional<ServerDescriptionPtr> _currentPrimary() const;

    mutable mongo::Mutex _mutex = mongo::Mutex(StringData("TopologyManager"));

    const SdamConfiguration _config;

    ClockSource* _clockSource;

    std::shared_ptr<TopologyDescription> _topologyDescription;
    std::unique_ptr<TopologyStateMachine> _topologyStateMachine;
};
}  // namespace mongo::sdam
