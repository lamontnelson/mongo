/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#include "opentracing/propagation.h"

#include "mongo/base/system_error.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/util/assert_util.h"

namespace mongo {

class BSONCarrierReader : public opentracing::TextMapReader {
public:
    explicit BSONCarrierReader(BSONObj obj);

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
}
