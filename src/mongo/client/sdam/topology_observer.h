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

#include "mongo/client/sdam/sdam_datatypes.h"
#include "mongo/client/sdam/server_description.h"

namespace mongo::sdam {
class TopologyObserver {
public:
    virtual void onTypeChange(TopologyType topologyType) = 0;
    virtual void onNewSetName(boost::optional<std::string> setName) = 0;
    virtual void onUpdatedServerType(const ServerDescription& serverDescription, ServerType newServerType) = 0;
    virtual void onNewMaxElectionId(const OID& newMaxElectionId) = 0;
    virtual void onNewMaxSetVersion(int newMaxSetVersion) = 0;
    virtual void onNewServerDescription(const ServerDescription& newServerDescription) = 0;
    virtual void onUpdateServerDescription(const ServerDescription& newServerDescription) = 0;
    virtual void onServerDescriptionRemoved(const ServerDescription& serverDescription) = 0;
};
}  // namespace mongo
