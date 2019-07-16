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

#include <string>

#include <opentracing/dynamic_load.h>
#include <opentracing/noop.h>

#include "mongo/unittest/unittest.h"
#include "mongo/util/log.h"
#include "mongo/util/str.h"

namespace mongo {
namespace {

TEST(SimpleTests, MakeNoopTracer) {
    auto tracer = opentracing::MakeNoopTracer();
}

TEST(SimpleTests, MakeSpan) {
    auto tracer = opentracing::MakeNoopTracer();
    auto span = tracer->StartSpan("root");
    span->Finish();
}

TEST(SimpleTests, LoadJaegerLibrary) {
    // Load the tracer library.
    std::string error_message;
    auto handle_maybe =
        opentracing::DynamicallyLoadTracingLibrary("libjaegertracing.so", error_message);
    invariant(handle_maybe,
              str::stream() << "Failed to load tracer library " << error_message << "\n");
}

class JaegerFixture : public unittest::Test {
public:
    void setUp() {
        static auto jaegerHandle = [] {
            std::string error_message;
            auto handle_maybe =
                opentracing::DynamicallyLoadTracingLibrary("libjaegertracing.so", error_message);
            invariant(handle_maybe,
                      str::stream() << "Failed to load tracer library " << error_message << "\n");
            return handle_maybe;
        }();
        
        _tracerFactory = &jaegerHandle->tracer_factory();
    }

protected:
    const opentracing::v2::TracerFactory* tracerFactory() const {
        return _tracerFactory;
    }

private:
    mutable const opentracing::v2::TracerFactory* _tracerFactory;
};

TEST_F(JaegerFixture, MakeJaegerTracer) {
    std::string errmsg;
    const auto config = R"(
service_name: jaegerTextFixture
disabled: false
reporter:
    logSpans: true
    localAgentHostPort: 10.1.2.24:6831 # JBR's workstation for testing only!
sampler:
  type: const
  param: 1)"_sd;
    auto expectedTracer = tracerFactory()->MakeTracer(config.rawData(), errmsg);
    invariant(expectedTracer, str::stream() << "Error making factory: " << errmsg);

    auto tracer = std::move(*expectedTracer);
    opentracing::Tracer::InitGlobal(tracer);
    auto rootSpan = tracer->StartSpan("root");
    auto childSpan = tracer->StartSpan("child", { opentracing::ChildOf(&rootSpan->context()) });
    childSpan->Log({{"msg", "Hello, world!"}});
    childSpan->Finish();
    rootSpan->Finish();

    tracer->Close(); 
}

}  // namespace
}  // namespace mongo
