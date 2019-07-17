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

#include "mongo/rpc/op_msg.h"

#include <opentracing/span.h>
#include <opentracing/tracer.h>

namespace mongo {
class ServiceContext;
class OperationContext;

Status setProcessParentSpan(const std::string& value);
void setupTracing(ServiceContext* service, std::string serviceName);
void shutdownTracing(ServiceContext* service);

namespace tracing {
using Tracer = opentracing::Tracer;
using Span = opentracing::Span;
using SpanContext = opentracing::SpanContext;
using SpanReference = opentracing::SpanReference;

extern thread_local Span* currentOpSpan;

inline SpanReference ChildOf(const SpanContext* span_context) noexcept {
    return opentracing::ChildOf(span_context);
}

inline SpanReference FollowsFrom(const SpanContext* span_context) noexcept {
    return opentracing::FollowsFrom(span_context);
}

inline opentracing::string_view fromStringData(StringData sd) noexcept {
    return opentracing::string_view(sd.rawData(), sd.size());
}

Tracer& getTracer();
std::unique_ptr<Span>& getServiceSpan(ServiceContext* service);
std::unique_ptr<Span>& getOperationSpan(OperationContext* opCtx);

const std::unique_ptr<Span>& getCurrentSpan(OperationContext* opCtx);

void configureOperationSpan(OperationContext* opCtx, const OpMsgRequest& request);

boost::optional<std::unique_ptr<SpanContext>> extractSpanContext(const BSONObj& body);
void injectSpanContext(const std::unique_ptr<Span>& span, BSONObjBuilder* out);

}  // namespace tracing
}  // namespace mongo
