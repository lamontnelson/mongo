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
ServerSelector::~ServerSelector() {}

SdamServerSelector::SdamServerSelector(const ServerSelectionConfiguration& config)
    : _config(config), _random(PseudoRandom(Date_t::now().asInt64())) {}

void SdamServerSelector::_getCandidateServers(std::vector<ServerDescriptionPtr>* result,
                                              const TopologyDescriptionPtr topologyDescription,
                                              const mongo::ReadPreferenceSetting& criteria) {
    switch (criteria.pref) {
        case ReadPreference::Nearest:
            *result = topologyDescription->findServers(nearestFilter(criteria));
            return;

        case ReadPreference::SecondaryOnly:
            *result = topologyDescription->findServers(secondaryFilter(criteria));
            return;

        case ReadPreference::PrimaryOnly: {
            const auto primaryCriteria = ReadPreferenceSetting(criteria.pref);
            *result = topologyDescription->findServers(primaryFilter(primaryCriteria));
            return;
        }

        case ReadPreference::PrimaryPreferred: {
            auto primaryCriteria = ReadPreferenceSetting(ReadPreference::PrimaryOnly);
            _getCandidateServers(result, topologyDescription, primaryCriteria);
            if (result->size())
                return;
            *result = topologyDescription->findServers(secondaryFilter(criteria));
        }

        case ReadPreference::SecondaryPreferred: {
            auto secondaryCriteria = criteria;
            secondaryCriteria.pref = ReadPreference::SecondaryOnly;
            _getCandidateServers(result, topologyDescription, secondaryCriteria);
            if (result->size())
                return;
            *result = topologyDescription->findServers(primaryFilter(criteria));
        }

        default:
            MONGO_UNREACHABLE
    }
}

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

    if (topologyDescription->getType() == TopologyType::kSingle) {
        auto servers = topologyDescription->getServers();
        return (servers.size() && servers[0]->getType() != ServerType::kUnknown)
            ? boost::optional<std::vector<ServerDescriptionPtr>>{{servers[0]}}
            : boost::none;
    }

    std::vector<ServerDescriptionPtr> results;
    _getCandidateServers(&results, topologyDescription, criteria);
    filterTags(&results, criteria.tags);

    if (results.size()) {
        ServerDescriptionPtr minServer =
            *std::min_element(results.begin(), results.end(), LatencyWindow::rttCompareFn);

        invariant(minServer->getRtt());
        auto latencyWindow = LatencyWindow(*minServer->getRtt(), _config.getLocalThresholdMs());
        latencyWindow.filterServers(&results);

        return results;
    }
    return boost::none;
}

ServerDescriptionPtr SdamServerSelector::_randomSelect(
    const std::vector<ServerDescriptionPtr>& servers) const {
    return servers[_random.nextInt64(servers.size())];
}

boost::optional<ServerDescriptionPtr> SdamServerSelector::selectServer(
    const TopologyDescriptionPtr topologyDescription, const ReadPreferenceSetting& criteria) {
    auto servers = selectServers(topologyDescription, criteria);
    return servers ? boost::optional<ServerDescriptionPtr>(_randomSelect(*servers)) : boost::none;
}

bool SdamServerSelector::_containsAllTags(ServerDescriptionPtr server, const BSONObj& tags) {
    auto serverTags = server->getTags();
    for (auto& checkTag : tags) {
        auto checkKey = checkTag.fieldName();
        auto checkValue = checkTag.String();
        auto pos = serverTags.find(checkKey);
        if (pos == serverTags.end() || pos->second != checkValue) {
            return false;
        }
    }
    return true;
}

void SdamServerSelector::filterTags(std::vector<ServerDescriptionPtr>* servers,
                                    const TagSet& tagSet) {
    const auto& checkTags = tagSet.getTagBSON();

    if (checkTags.nFields() == 0)
        return;

    const auto predicate = [&](const ServerDescriptionPtr& s) {
        auto it = checkTags.begin();
        while (it != checkTags.end()) {
            if (_containsAllTags(s, it->Obj())) {
                // found a match -- don't remove the server
                return false;
            }
            ++it;
        }
        return true;
    };

    servers->erase(std::remove_if(servers->begin(), servers->end(), predicate), servers->end());
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
