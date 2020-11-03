/**
 *    Copyright (C) 2020-present MongoDB, Inc.
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

#include "mongo/s/query/sharded_agg_util.h"
#include "mongo/db/pipeline/aggregation_request.h"
#include "mongo/db/pipeline/process_interface/mongos_process_interface.h"
#include "mongo/db/query/collation/collator_factory_interface.h"
#include "mongo/executor/task_executor_pool.h"
#include "mongo/s/grid.h"

namespace mongo::sharded_agg_util {

StringMap<ExpressionContext::ResolvedNamespace> resolveInvolvedNamespaces(
    stdx::unordered_set<NamespaceString> involvedNamespaces) {
    StringMap<ExpressionContext::ResolvedNamespace> resolvedNamespaces;
    for (auto&& nss : involvedNamespaces) {
        resolvedNamespaces.try_emplace(nss.coll(), nss, std::vector<BSONObj>{});
    }
    return resolvedNamespaces;
}

boost::intrusive_ptr<ExpressionContext> makeExpressionContext(
    OperationContext* opCtx,
    const AggregationRequest& request,
    BSONObj collationObj,
    boost::optional<UUID> uuid,
    StringMap<ExpressionContext::ResolvedNamespace> resolvedNamespaces,
    bool isMongos) {

    std::unique_ptr<CollatorInterface> collation;
    if (!collationObj.isEmpty()) {
        // This will be null if attempting to build an interface for the simple collator.
        collation = uassertStatusOK(
            CollatorFactoryInterface::get(opCtx->getServiceContext())->makeFromBSON(collationObj));
    }

    // Create the expression context, and set 'inMongos' to true. We explicitly do *not* set
    // mergeCtx->tempDir.
    auto mergeCtx = make_intrusive<ExpressionContext>(
        opCtx,
        request,
        std::move(collation),
        std::make_shared<MongosProcessInterface>(
            Grid::get(opCtx)->getExecutorPool()->getArbitraryExecutor()),
        std::move(resolvedNamespaces),
        uuid);

    mergeCtx->inMongos = isMongos;
    return mergeCtx;
}

}  // namespace mongo::sharded_agg_util