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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include <opentracing/dynamic_load.h>
#include <opentracing/propagation.h>

#include "mongo/base/system_error.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/db/tracing/tracing.h"
#include "mongo/util/decorable.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {
const auto kJaegerLibraryName = "libjaegertracing.so";
const auto kTracerConfigFormat = R"(
service_name: {}
disabled: false
reporter:
    logSpans: true
    localAgentHostPort: 10.1.2.24:6831 # JBR's workstation for testing only!
sampler:
  type: const
  param: 1)"_sd;

const auto getServiceDecoration =
    ServiceContext::declareDecoration<std::unique_ptr<tracing::Span>>();
const auto getOperationDecoration =
    OperationContext::declareDecoration<std::unique_ptr<tracing::Span>>();

class BSONCarrierReader : public opentracing::TextMapReader {
public:
    explicit BSONCarrierReader(BSONObj obj) : _obj(obj) {}

    opentracing::expected<void> ForeachKey(
        std::function<opentracing::expected<void>(
            opentracing::string_view key, opentracing::string_view value)> func) const override
        try {
        for (const auto& kv : _obj) {
            func(fromStringData(kv.fieldNameStringData()),
                 fromStringData(kv.checkAndGetStringData().rawData()));
        }

        return {};
    } catch (const DBException& e) {
        return opentracing::make_unexpected(std::error_code(e.code(), mongoErrorCategory()));
    }

private:
    opentracing::string_view fromStringData(StringData data) const {
        return opentracing::string_view(data.rawData(), data.size());
    }
    BSONObj _obj;
};

class BSONCarrierWriter : public opentracing::TextMapWriter {
public:
    opentracing::expected<void> Set(opentracing::string_view key,
                                    opentracing::string_view value) const override {
        _bob.append(fromStringView(key), fromStringView(value));
        return {};
    }

    BSONObj obj() {
        return _bob.obj();
    }

private:
    StringData fromStringView(opentracing::string_view view) const {
        return StringData(view.data(), view.size());
    }

    mutable BSONObjBuilder _bob;
};
}  // namespace

namespace tracing {
Tracer& getTracer() {
    return *Tracer::Global();
}

std::unique_ptr<Span>& getServiceSpan(ServiceContext* service) {
    return getServiceDecoration(service);
}

std::unique_ptr<Span>& getOperationSpan(OperationContext* opCtx) {
    return getOperationDecoration(opCtx);
}

void setupTracing(ServiceContext* service, std::string serviceName) {
    std::string errorMessage;

    auto handleMaybe = opentracing::DynamicallyLoadTracingLibrary(kJaegerLibraryName, errorMessage);
    if (!handleMaybe) {
        severe() << "Failed to load tracer library " << kJaegerLibraryName << ": " << errorMessage;
        fassertFailed(31184);
    }

    auto& factory = handleMaybe->tracer_factory();
    auto config = fmt::format(kTracerConfigFormat, serviceName);
    auto tracer = factory.MakeTracer(config.data(), errorMessage);
    if (!tracer) {
        severe() << "Error creating tracer: " << errorMessage;
        fassertFailed(31185);
    }

    opentracing::Tracer::InitGlobal(*tracer);

    tracing::getServiceSpan(service) = (*tracer)->StartSpan(serviceName);
    LOG(1) << "initialized opentracing for " << serviceName;
}

void shutdownTracing(ServiceContext* service) {
    auto serviceSpan = std::move(tracing::getServiceSpan(service));
    serviceSpan->Log({{"msg", "shutting down"}});
    serviceSpan->Finish();
    tracing::getTracer().Close();
    LOG(1) << "shut down opentracing";
}

void configureOperationSpan(OperationContext* opCtx, const OpMsgRequest& request) {
    // setup open tracing
    auto spanContext = tracing::extractSpanContext(request.body);
    auto& tracer = tracing::getTracer();
    auto svcCtx = opCtx->getServiceContext();
    if (spanContext) {
        auto opSpan =
            tracer.StartSpan(request.getCommandName().rawData(),
                             {tracing::ChildOf(spanContext->get()),
                              tracing::ChildOf(&tracing::getServiceSpan(svcCtx)->context())});
        getOperationSpan(opCtx).swap(opSpan);
    } else {
        auto opSpan =
            tracer.StartSpan(request.getCommandName().rawData(),
                             {tracing::ChildOf(&tracing::getServiceSpan(svcCtx)->context())});
        getOperationSpan(opCtx).swap(opSpan);
    }
}

boost::optional<std::unique_ptr<SpanContext>> extractSpanContext(const BSONObj& body) {
    auto elem = body.getField("$spanContext");
    if (elem.eoo()) {
        return boost::none;
    }

    auto expectedSpanContext = getTracer().Extract(BSONCarrierReader(elem.Obj()));
    uassert(51243, "Failed to extract span context", expectedSpanContext);
    return std::move(*expectedSpanContext);
}

void injectSpanContext(const std::unique_ptr<Span>& span, BSONObjBuilder* out) {
    BSONCarrierWriter carrier;
    uassert(51242, "Failed to inject span context", getTracer().Inject(span->context(), carrier));
    out->append("$spanContext", carrier.obj());
}

}  // namespace tracing
}  // namespace mongo
