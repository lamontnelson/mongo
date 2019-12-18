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
#include "server_selector.h"

#include <algorithm>

#include "mongo/client/sdam/topology_description.h"
#include "mongo/platform/random.h"

namespace mongo::sdam {
SdamServerSelector::SdamServerSelector(const ServerSelectionConfiguration& config)
    : _config(config), _random(PseudoRandom(Date_t::now().asInt64())) {}

/*
If the topology wire version is invalid, raise an error
Find suitable servers by topology type and operation type
Filter the suitable servers by calling the optional, application-provided server selector.
If there are any suitable servers, choose one at random from those within the latency window and
return it; otherwise, continue to the next step Request an immediate topology check, then block the
server selection thread until the topology changes or until the server selection timeout has elapsed
If more than serverSelectionTimeoutMS milliseconds have elapsed since the selection start time,
raise a server selection error Goto Step #2
*/
boost::optional<std::vector<ServerDescriptionPtr>> SdamServerSelector::selectServers(
    const TopologyDescriptionPtr topologyDescription,
    const mongo::ReadPreferenceSetting& criteria) {

    // If the topology wire version is invalid, raise an error
    if (!topologyDescription->isWireVersionCompatible()) {
        uasserted(ErrorCodes::IncompatibleServerVersion,
                  *topologyDescription->getWireVersionCompatibleError());
    }

    if (topologyDescription->getType() == TopologyType::kUnknown) {
        return boost::none;
    }

    std::vector<ServerDescriptionPtr> candidateServers = []() {
    switch (criteria.pref) {
        case ReadPreference::Nearest:
            return topologyDescription->getServers();
        case ReadPreference::SecondaryOnly:
            return topologyDescription->findServers(secondaryFilter);
        case ReadPreference::PrimaryOnly:
			
            break;
        case ReadPreference::PrimaryPreferred:
        case ReadPreference::SecondaryPreferred:
            break;
    }
	}();
}


ServerDescriptionPtr SdamServerSelector::selectRandomly(
    const std::vector<ServerDescriptionPtr>& servers) const {
    int64_t index = _random.nextInt64(servers.size() - 1);
    std::cout << "numServers: " << servers.size() << "chose: " << index << std::endl;
    return servers[index];
}

boost::optional<ServerDescriptionPtr> SdamServerSelector::selectServer(
    const TopologyDescriptionPtr topologyDescription, const ReadPreferenceSetting& criteria) {
    auto servers = selectServers(topologyDescription, criteria);
    if (servers) {
        // TODO: remove this scan; should probably keep the list sorted by latency
        ServerDescriptionPtr minServer =
            *std::min_element(servers->begin(), servers->end(), LatencyWindow::rttCompareFn);

        // TODO: this assumes that Rtt is always present; fix
        auto latencyWindow = LatencyWindow(*minServer->getRtt(), _config.getLocalThresholdMs());
        latencyWindow.filterServers(&(*servers));

        return selectRandomly(*servers);
    }

    return boost::none;
}

void LatencyWindow::filterServers(std::vector<ServerDescriptionPtr>* servers) {
    servers->erase(std::remove_if(servers->begin(),
                                  servers->end(),
                                  [&](const ServerDescriptionPtr& s) {
                                      return !this->isWithinWindow(*s->getRtt());
                                  }),
                   servers->end());
}

bool LatencyWindow::isWithinWindow(IsMasterRTT latency) {
    return lower <= latency && latency <= upper;
}
}  // namespace mongo::sdam
