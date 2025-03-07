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

#include "server_config.hpp"

#include <memory>

#include <catch2/catch.hpp>

#include <silkworm/node/test/log.hpp>

namespace silkworm::rpc {

// Exclude gRPC tests from sanitizer builds due to data race warnings inside gRPC library
#ifndef SILKWORM_SANITIZE
TEST_CASE("ServerConfig::ServerConfig", "[silkworm][rpc][server_config]") {
    test::SetLogVerbosityGuard log_guard{log::Level::kNone};
    ServerConfig config;
    CHECK(config.address_uri() == kDefaultAddressUri);
    CHECK(config.num_contexts() == kDefaultNumContexts);
}

TEST_CASE("ServerConfig::set_address_uri", "[silkworm][rpc][server_config]") {
    test::SetLogVerbosityGuard log_guard{log::Level::kNone};
    const std::string address_uri{"127.0.0.1:12345"};
    ServerConfig config;
    config.set_address_uri(address_uri);
    CHECK(config.address_uri() == address_uri);
}

TEST_CASE("ServerConfig::set_num_contexts", "[silkworm][rpc][server_config]") {
    test::SetLogVerbosityGuard log_guard{log::Level::kNone};
    const uint32_t num_contexts{10};
    ServerConfig config;
    config.set_num_contexts(num_contexts);
    CHECK(config.num_contexts() == num_contexts);
}

TEST_CASE("ServerConfig::set_credentials", "[silkworm][rpc][server_config]") {
    test::SetLogVerbosityGuard log_guard{log::Level::kNone};
    grpc::SslServerCredentialsOptions ssl_options;
    const std::shared_ptr<grpc::ServerCredentials> server_credentials{
        grpc::SslServerCredentials(ssl_options)};
    ServerConfig config;
    config.set_credentials(server_credentials);
    CHECK(config.credentials() == server_credentials);
}
#endif  // SILKWORM_SANITIZE

}  // namespace silkworm::rpc
