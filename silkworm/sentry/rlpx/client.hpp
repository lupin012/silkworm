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

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <silkworm/node/concurrency/coroutine.hpp>

#include <boost/asio/awaitable.hpp>

#include <silkworm/sentry/common/ecc_key_pair.hpp>
#include <silkworm/sentry/common/enode_url.hpp>

#include "peer.hpp"
#include "protocol.hpp"

namespace silkworm::sentry::rlpx {

class Client {
  public:
    Client(
        common::EccKeyPair node_key,
        std::string client_id,
        uint16_t node_listen_port,
        std::function<std::unique_ptr<Protocol>()> protocol_factory)
        : node_key_(std::move(node_key)),
          client_id_(std::move(client_id)),
          node_listen_port_(node_listen_port),
          protocol_factory_(std::move(protocol_factory)) {
    }

    boost::asio::awaitable<std::unique_ptr<Peer>> connect(
        common::EnodeUrl peer_url,
        bool is_static_peer);

  private:
    common::EccKeyPair node_key_;
    std::string client_id_;
    uint16_t node_listen_port_;
    std::function<std::unique_ptr<Protocol>()> protocol_factory_;
};

}  // namespace silkworm::sentry::rlpx
