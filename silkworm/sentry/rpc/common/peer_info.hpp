/*
   Copyright 2023 The Silkworm Authors

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

#include <string>
#include <vector>

#include <boost/asio/ip/tcp.hpp>

#include <silkworm/sentry/common/ecc_public_key.hpp>
#include <silkworm/sentry/common/enode_url.hpp>

namespace silkworm::sentry::rpc::common {

struct PeerInfo {
    sentry::common::EnodeUrl url;
    sentry::common::EccPublicKey peer_public_key;
    boost::asio::ip::tcp::endpoint local_endpoint;
    boost::asio::ip::tcp::endpoint remote_endpoint;
    bool is_inbound;
    bool is_static;
    std::string client_id;
    std::vector<std::string> capabilities;
};

using PeerInfos = std::vector<PeerInfo>;

}  // namespace silkworm::sentry::rpc::common
