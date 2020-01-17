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

#include "mongo/client/sdam/topology_listener.h"

namespace mongo::sdam {
void TopologyEventsPublisher::registerListener(TopologyListenerPtr listener) {
    stdx::lock_guard<Mutex> lk(_mutex);
    std::cout << "register listener" << std::endl;
    _listeners.push_back(listener);
}

void TopologyEventsPublisher::removeListener(TopologyListenerPtr listener) {
    stdx::lock_guard<Mutex> lk(_mutex);
    std::cout << "de-register listener" << std::endl;
    _listeners.erase(std::remove(_listeners.begin(), _listeners.end(), listener), _listeners.end());
}

void TopologyEventsPublisher::close() {
    stdx::lock_guard<Mutex> lk(_mutex);
    _listeners.clear();
    _executor = nullptr;
}

void TopologyEventsPublisher::onTopologyDescriptionChangedEvent(
    UUID topologyId,
    TopologyDescriptionPtr previousDescription,
    TopologyDescriptionPtr newDescription) {
    run([=, self = shared_from_this()](auto&&) {
        stdx::lock_guard<Mutex> lk(self->_mutex);
        for (auto listener : self->_listeners) {
            listener->onTopologyDescriptionChangedEvent(
                topologyId, previousDescription, newDescription);
        }
    });
}

//TODO: fix this implementation; since events are launched async we need to order them
void TopologyEventsPublisher::onServerHeartbeatSucceededEvent(mongo::Milliseconds durationMs,
                                                              ServerAddress hostAndPort,
                                                              const BSONObj reply) {
    run([=, self = shared_from_this()](auto&&) {
        stdx::lock_guard<Mutex> lk(self->_mutex);
        for (auto listener : self->_listeners) {
            listener->onServerHeartbeatSucceededEvent(durationMs, hostAndPort, reply);
        }
    });
}

void TopologyEventsPublisher::onServerHeartbeatFailureEvent(mongo::Milliseconds durationMs,
                                                            Status errorStatus,
                                                            ServerAddress hostAndPort,
                                                            const BSONObj reply) {
    run([=, self = shared_from_this()](auto&&) {
        stdx::lock_guard<Mutex> lk(self->_mutex);
        for (auto listener : self->_listeners) {
            listener->onServerHeartbeatFailureEvent(durationMs, errorStatus, hostAndPort, reply);
        }
    });
}

void TopologyEventsPublisher::onServerPingFailedEvent(const ServerAddress hostAndPort,
                                                      const Status& status) {
    run([=, self = shared_from_this()](auto&&) {
      stdx::lock_guard<Mutex> lk(self->_mutex);
      for (auto listener : self->_listeners) {
          listener->onServerPingFailedEvent(hostAndPort, status);
      }
    });
}

void TopologyEventsPublisher::onServerPingSucceededEvent(mongo::Milliseconds durationMS,
                                                         ServerAddress hostAndPort) {
    run([=, self = shared_from_this()](auto&&) {
      stdx::lock_guard<Mutex> lk(self->_mutex);
      for (auto listener : self->_listeners) {
          listener->onServerPingSucceededEvent(durationMS, hostAndPort);
      }
    });
}

void TopologyEventsPublisher::run(OutOfLineExecutor::Task functor) {
    _executor->schedule(std::move(functor));
}
};  // namespace mongo::sdam
