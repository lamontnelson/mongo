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
#include <vector>

#include "mongo/client/read_preference.h"
#include "mongo/client/sdam/sdam_configuration.h"
#include "mongo/client/sdam/sdam_datatypes.h"
#include "mongo/client/sdam/server_description.h"
#include "mongo/platform/random.h"

namespace mongo::sdam {
/**
 * This is the interface that allows one to select a server to satisfy a DB operation given a
 * TopologyDescription and a ReadPreferenceSetting.
 *
 * This is exposed as an interface so that tests can feel free to use their own version of the
 * server selection algorithm if necessary.
 */
class ServerSelector {
public:
    /**
     * Finds a list of candidate servers according to the ReadPreferenceSetting.
     */
    virtual boost::optional<std::vector<ServerDescriptionPtr>> selectServers(
        TopologyDescriptionPtr topologyDescription, const ReadPreferenceSetting& criteria) = 0;

    /**
     * Select a single server according to the ReadPreference and latency of the ServerDescription(s)
     */
    virtual boost::optional<ServerDescriptionPtr> selectServer(
        const TopologyDescriptionPtr topologyDescription,
        const ReadPreferenceSetting& criteria) = 0;

    virtual ~ServerSelector() {};
};
using ServerSelectorPtr = std::unique_ptr<ServerSelector>;

class SdamServerSelector : public ServerSelector {
public:
    SdamServerSelector(const ServerSelectionConfiguration& config);

    boost::optional<std::vector<ServerDescriptionPtr>> selectServers(
        const TopologyDescriptionPtr topologyDescription,
        const ReadPreferenceSetting& criteria) override;

    boost::optional<ServerDescriptionPtr> selectServer(
        const TopologyDescriptionPtr topologyDescription, const ReadPreferenceSetting& criteria);

    // remove servers that do not match the TagSet
    void filterTags(std::vector<ServerDescriptionPtr>* servers, const TagSet& tagSet);

private:
    ServerDescriptionPtr _randomSelect(const std::vector<ServerDescriptionPtr>& servers) const;
    void _getCandidateServers(std::vector<ServerDescriptionPtr>* result,
                              const TopologyDescriptionPtr topologyDescription,
                              const ReadPreferenceSetting& criteria);
    bool _containsAllTags(ServerDescriptionPtr server, const BSONObj& tags);

    static const inline auto recencyFilter = [](const ReadPreferenceSetting& readPref,
                                                const ServerDescriptionPtr& s) {
        bool result = true;

        if (!readPref.minOpTime.isNull()) {
            result = result && readPref.minOpTime <= s->getOpTime();
        }

        if (readPref.maxStalenessSeconds.count()) {
            const Date_t minWriteDate = Date_t::now() - Seconds(readPref.maxStalenessSeconds);
            result = result && minWriteDate <= s->getLastWriteDate();
        }

        return result;
    };

    static const inline auto secondaryFilter = [](const ReadPreferenceSetting& readPref) {
        return [&](const ServerDescriptionPtr& s) {
            return s->getType() == ServerType::kRSSecondary && recencyFilter(readPref, s);
        };
    };

    static const inline auto primaryFilter = [](const ReadPreferenceSetting& readPref) {
        return [&](const ServerDescriptionPtr& s) {
            return s->getType() == ServerType::kRSPrimary && recencyFilter(readPref, s);
        };
    };

    static const inline auto nearestFilter = [](const ReadPreferenceSetting& readPref) {
        return [&](const ServerDescriptionPtr& s) {
            return s->getType() != ServerType::kUnknown && recencyFilter(readPref, s);
        };
    };

    ServerSelectionConfiguration _config;
    mutable PseudoRandom _random;
};

struct LatencyWindow {
    IsMasterRTT lower;
    IsMasterRTT upper;

    LatencyWindow(const IsMasterRTT lowerBound, const IsMasterRTT windowWidth)
        : lower(lowerBound), upper(lowerBound + windowWidth) {}

    bool isWithinWindow(IsMasterRTT latency);

    // remove servers not in the latency window in-place.
    void filterServers(std::vector<ServerDescriptionPtr>* servers);

    static auto inline rttCompareFn = [](ServerDescriptionPtr a, ServerDescriptionPtr b) {
        return a->getRtt() < b->getRtt();
    };
};
}  // namespace mongo::sdam
