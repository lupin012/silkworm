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

#include "genesis.hpp"

#include <catch2/catch.hpp>

#include <silkworm/core/chain/genesis.hpp>
#include <silkworm/node/common/test_context.hpp>
#include <silkworm/node/db/tables.hpp>

namespace silkworm {

namespace db {

    TEST_CASE("Database genesis initialization") {
        test::Context context;
        auto& txn{context.rw_txn()};

        SECTION("Initialize with Mainnet") {
            auto source_data{silkworm::read_genesis_data(silkworm::kMainnetConfig.chain_id)};
            auto genesis_json = nlohmann::json::parse(source_data, nullptr, /*allow_exceptions=*/false);
            REQUIRE(db::initialize_genesis(txn, genesis_json, /*allow_exceptions=*/false));
            context.commit_and_renew_txn();
            CHECK(db::read_chain_config(txn) == silkworm::kMainnetConfig);
        }
        SECTION("Initialize with Goerli") {
            auto source_data{silkworm::read_genesis_data(silkworm::kGoerliConfig.chain_id)};
            auto genesis_json = nlohmann::json::parse(source_data, nullptr, /*allow_exceptions=*/false);
            REQUIRE(db::initialize_genesis(txn, genesis_json, /*allow_exceptions=*/false));
            CHECK(db::read_chain_config(txn) == silkworm::kGoerliConfig);
        }
        SECTION("Initialize with Rinkeby") {
            auto source_data{silkworm::read_genesis_data(silkworm::kRinkebyConfig.chain_id)};
            auto genesis_json = nlohmann::json::parse(source_data, nullptr, /*allow_exceptions=*/false);
            REQUIRE(db::initialize_genesis(txn, genesis_json, /*allow_exceptions=*/false));
            CHECK(db::read_chain_config(txn) == silkworm::kRinkebyConfig);
        }
        SECTION("Initialize with Sepolia") {
            auto source_data{silkworm::read_genesis_data(silkworm::kSepoliaConfig.chain_id)};
            auto genesis_json = nlohmann::json::parse(source_data, nullptr, /*allow_exceptions=*/false);
            REQUIRE(db::initialize_genesis(txn, genesis_json, /*allow_exceptions=*/false));
            CHECK(db::read_chain_config(txn) == silkworm::kSepoliaConfig);
        }

        SECTION("Initialize with invalid Json") {
            std::string source_data{"{chainId="};
            auto genesis_json = nlohmann::json::parse(source_data, nullptr, /*allow_exceptions=*/false);
            REQUIRE_THROWS(db::initialize_genesis(txn, genesis_json, /*allow_exceptions=*/true));
        }
#if defined(__clang__) && !defined(NDEBUG)
        // clang has a defect so throw-ing T (a generic exception) is not catch-ed
#else
        SECTION("Initialize with errors in Json payload") {
            // Base is mainnet
            auto source_data{silkworm::read_genesis_data(silkworm::kMainnetConfig.chain_id)};
            nlohmann::json notHex = "0xgg";

            // Remove mandatory members
            {
                auto genesis_json = nlohmann::json::parse(source_data, nullptr, /*allow_exceptions=*/false);
                REQUIRE(genesis_json.is_discarded() == false);
                auto removed_count = genesis_json.erase("difficulty");
                removed_count += genesis_json.erase("gaslimit");
                removed_count += genesis_json.erase("timestamp");
                removed_count += genesis_json.erase("extraData");
                removed_count += genesis_json.erase("config");
                const auto& [valid, errors]{db::validate_genesis_json(genesis_json)};
                REQUIRE(valid == false);
                CHECK(errors.size() == removed_count);
            }

            // Tamper with hex values
            {
                auto genesis_json = nlohmann::json::parse(source_data, nullptr, /*allow_exceptions=*/false);
                REQUIRE(genesis_json.is_discarded() == false);
                genesis_json["difficulty"] = notHex;
                genesis_json["nonce"] = notHex;
                const auto& [valid, errors]{db::validate_genesis_json(genesis_json)};
                REQUIRE(valid == false);
                CHECK(errors.size() == 2);

                genesis_json = nlohmann::json::parse(source_data, nullptr, /*allow_exceptions=*/false);
                genesis_json["alloc"]["c951900c341abbb3bafbf7ee2029377071dbc36a"]["balance"] = notHex;
            }

            // Tamper with hex values on allocations
            {
                auto genesis_json = nlohmann::json::parse(source_data, nullptr, /*allow_exceptions=*/false);
                genesis_json["alloc"]["c951900c341abbb3bafbf7ee2029377071dbc36a"]["balance"] = notHex;
                genesis_json["alloc"]["c951900c341abbb3bafbf7ee2029377071dbc"]["balance"] = notHex;
                const auto& [valid, errors]{db::validate_genesis_json(genesis_json)};
                REQUIRE(valid == false);
                CHECK(errors.size() == 2);
            }

            // Remove chainId from config member
            {
                auto genesis_json = nlohmann::json::parse(source_data, nullptr, /*allow_exceptions=*/false);
                genesis_json["config"].erase("chainId");
                const auto& [valid, errors]{db::validate_genesis_json(genesis_json)};
                REQUIRE(valid == false);
                CHECK(errors.size() == 1);
            }
        }
#endif  // non-clang

        SECTION("Update chain config") {
            SECTION("Without genesis block") {
                // Nothing should happen
                db::update_chain_config(txn, silkworm::kMainnetConfig);
                db::PooledCursor config(txn, db::table::kConfig);
                REQUIRE(config.empty());
            }

            SECTION("With genesis block") {
                auto source_data{silkworm::read_genesis_data(silkworm::kMainnetConfig.chain_id)};
                auto genesis_json = nlohmann::json::parse(source_data, nullptr, /*allow_exceptions=*/false);
                REQUIRE(db::initialize_genesis(txn, genesis_json, /*allow_exceptions=*/false));
                context.commit_and_renew_txn();
                CHECK(db::read_chain_config(txn) == silkworm::kMainnetConfig);

                // Now update with sepolia chain config
                // Yes it should not happen and is wrong - but only needed to test a new
                db::update_chain_config(txn, silkworm::kSepoliaConfig);
                REQUIRE(db::read_chain_config(txn) == silkworm::kSepoliaConfig);
            }
        }
    }

}  // namespace db
}  // namespace silkworm
