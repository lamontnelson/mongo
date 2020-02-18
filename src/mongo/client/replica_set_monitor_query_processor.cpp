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
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork
#include "replica_set_monitor_query_processor.h"

#include "mongo/client/global_conn_pool.h"
#include "mongo/util/log.h"

namespace mongo {
void ReplicaSetMonitorQueryProcessor::shutdown() {
    stdx::lock_guard lock(_mutex);
    _isShutdown = true;
}

void ReplicaSetMonitorQueryProcessor::onTopologyDescriptionChangedEvent(
    UUID topologyId,
    sdam::TopologyDescriptionPtr previousDescription,
    sdam::TopologyDescriptionPtr newDescription) {
    {
        stdx::lock_guard lock(_mutex);
        if (_isShutdown)
            return;
    }

    const auto& setName = newDescription->getSetName();
    if (setName) {
        auto replicaSetMonitor = globalRSMonitorManager.getMonitor(*setName);
        if (!replicaSetMonitor) {
            LOG(kLogLevel) << "could not find rsm instance " << *setName << " for query processing.";
            return;
        }
        replicaSetMonitor->_processOutstanding(newDescription);
    }

    // No set name occurs when there is an error monitoring isMaster replies (e.g. HostUnreachable).
    // There is nothing to do in that case.
}
};  // namespace mongo
