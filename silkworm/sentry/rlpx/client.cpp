/*
   Copyright 2022 The Silkworm Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "client.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/system_error.hpp>

#include <silkworm/node/common/log.hpp>
#include <silkworm/sentry/common/sleep.hpp>
#include <silkworm/sentry/common/socket_stream.hpp>

namespace silkworm::sentry::rlpx {

using namespace std::chrono_literals;
using namespace boost::asio;

awaitable<std::unique_ptr<Peer>> Client::connect(
    common::EnodeUrl peer_url,
    bool is_static_peer) {
    log::Debug() << "RLPx client connecting to " << peer_url.to_string();

    auto client_context = co_await boost::asio::this_coro::executor;

    ip::tcp::resolver resolver{client_context};
    auto endpoints = co_await resolver.async_resolve(
        peer_url.ip().to_string(),
        std::to_string(peer_url.port()),
        use_awaitable);
    const ip::tcp::endpoint& endpoint = *endpoints.cbegin();

    common::SocketStream stream{client_context};

    bool is_connected = false;
    while (!is_connected) {
        try {
            co_await stream.socket().async_connect(endpoint, use_awaitable);
            is_connected = true;
        } catch (const boost::system::system_error& ex) {
            if (ex.code() == boost::system::errc::operation_canceled)
                throw;
            log::Debug() << "RLPx client connect exception: " << ex.what();
        }
        if (!is_connected) {
            stream = common::SocketStream{client_context};
            log::Warning() << "Failed to connect to " << peer_url.to_string() << ", reconnecting...";
            co_await common::sleep(10s);
        }
    }

    auto remote_endpoint = stream.socket().remote_endpoint();
    log::Debug() << "RLPx client connected to "
                 << remote_endpoint.address().to_string() << ":" << remote_endpoint.port();

    co_return std::make_unique<Peer>(
        client_context,
        std::move(stream),
        node_key_,
        client_id_,
        node_listen_port_,
        protocol_factory_(),
        std::optional{peer_url},
        std::optional{peer_url.public_key()},
        /* is_inbound = */ false,
        /* is_static = */ is_static_peer);
}

}  // namespace silkworm::sentry::rlpx
