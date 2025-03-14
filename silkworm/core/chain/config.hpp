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

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>

#include <evmc/evmc.h>
#include <intx/intx.hpp>
#include <nlohmann/json.hpp>

#include <silkworm/core/common/util.hpp>

namespace silkworm {

enum class SealEngineType {
    kNoProof,
    kEthash,
    kClique,
    kAuRA,
};

struct ChainConfig {
    //! \brief Returns the chain identifier
    //! \see https://eips.ethereum.org/EIPS/eip-155
    uint64_t chain_id{0};

    //! \brief Holds the hash of genesis block
    std::optional<evmc::bytes32> genesis_hash;

    //! \brief Returns the type of seal engine
    SealEngineType seal_engine{SealEngineType::kNoProof};

    // https://github.com/ethereum/execution-specs/tree/master/network-upgrades/mainnet-upgrades
    std::optional<BlockNum> homestead_block{std::nullopt};
    std::optional<BlockNum> dao_block{std::nullopt};
    std::optional<BlockNum> tangerine_whistle_block{std::nullopt};
    std::optional<BlockNum> spurious_dragon_block{std::nullopt};
    std::optional<BlockNum> byzantium_block{std::nullopt};
    std::optional<BlockNum> constantinople_block{std::nullopt};
    std::optional<BlockNum> petersburg_block{std::nullopt};
    std::optional<BlockNum> istanbul_block{std::nullopt};
    std::optional<BlockNum> muir_glacier_block{std::nullopt};
    std::optional<BlockNum> berlin_block{std::nullopt};
    std::optional<BlockNum> london_block{std::nullopt};
    std::optional<BlockNum> arrow_glacier_block{std::nullopt};
    std::optional<BlockNum> gray_glacier_block{std::nullopt};

    //! \brief PoW to PoS switch
    //! \see EIP-3675: Upgrade consensus to Proof-of-Stake
    std::optional<intx::uint256> terminal_total_difficulty{std::nullopt};
    std::optional<BlockNum> merge_netsplit_block{std::nullopt};  // FORK_NEXT_VALUE in EIP-3675

    // Starting from Shanghai, forks are triggered by block time rather than number
    std::optional<uint64_t> shanghai_time{std::nullopt};
    std::optional<uint64_t> cancun_time{std::nullopt};

    //! \brief Returns the revision level at given block number
    //! \details In other words, on behalf of Json chain config data
    //! returns whether specific HF have occurred
    [[nodiscard]] evmc_revision revision(uint64_t block_number, uint64_t block_time) const noexcept;

    [[nodiscard]] std::vector<BlockNum> distinct_fork_numbers() const;

    //! \brief Return the JSON representation of this object
    [[nodiscard]] nlohmann::json to_json() const noexcept;

    /*Sample JSON input:
    {
            "chainId":1,
            "homesteadBlock":1150000,
            "daoForkBlock":1920000,
            "eip150Block":2463000,
            "eip155Block":2675000,
            "byzantiumBlock":4370000,
            "constantinopleBlock":7280000,
            "petersburgBlock":7280000,
            "istanbulBlock":9069000,
            "muirGlacierBlock":9200000,
            "berlinBlock":12244000
    }
    */
    //! \brief Try parse a JSON object into strongly typed ChainConfig
    //! \remark Should this return std::nullopt the parsing has failed
    static std::optional<ChainConfig> from_json(const nlohmann::json& json) noexcept;

    friend bool operator==(const ChainConfig&, const ChainConfig&) = default;
};

std::ostream& operator<<(std::ostream& out, const ChainConfig& obj);

inline constexpr evmc::bytes32 kMainnetGenesisHash{0xd4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3_bytes32};
inline constexpr ChainConfig kMainnetConfig{
    .chain_id = 1,
    .seal_engine = SealEngineType::kEthash,
    .homestead_block = 1'150'000,
    .dao_block = 1'920'000,
    .tangerine_whistle_block = 2'463'000,
    .spurious_dragon_block = 2'675'000,
    .byzantium_block = 4'370'000,
    .constantinople_block = 7'280'000,
    .petersburg_block = 7'280'000,
    .istanbul_block = 9'069'000,
    .muir_glacier_block = 9'200'000,
    .berlin_block = 12'244'000,
    .london_block = 12'965'000,
    .arrow_glacier_block = 13'773'000,
    .gray_glacier_block = 15'050'000,
    .terminal_total_difficulty = intx::from_string<intx::uint256>("58750000000000000000000"),
};

inline constexpr evmc::bytes32 kRinkebyGenesisHash{0x6341fd3daf94b748c72ced5a5b26028f2474f5f00d824504e4fa37a75767e177_bytes32};
inline constexpr ChainConfig kRinkebyConfig{
    .chain_id = 4,
    .seal_engine = SealEngineType::kClique,
    .homestead_block = 1,
    .tangerine_whistle_block = 2,
    .spurious_dragon_block = 3,
    .byzantium_block = 1'035'301,
    .constantinople_block = 3'660'663,
    .petersburg_block = 4'321'234,
    .istanbul_block = 5'435'345,
    .berlin_block = 8'290'928,
    .london_block = 8'897'988,
};

inline constexpr evmc::bytes32 kGoerliGenesisHash{0xbf7e331f7f7c1dd2e05159666b3bf8bc7a8a3a9eb1d518969eab529dd9b88c1a_bytes32};
inline constexpr ChainConfig kGoerliConfig{
    .chain_id = 5,
    .seal_engine = SealEngineType::kClique,
    .homestead_block = 0,
    .tangerine_whistle_block = 0,
    .spurious_dragon_block = 0,
    .byzantium_block = 0,
    .constantinople_block = 0,
    .petersburg_block = 0,
    .istanbul_block = 1'561'651,
    .berlin_block = 4'460'644,
    .london_block = 5'062'605,
    .terminal_total_difficulty = 10790000,
};

inline constexpr evmc::bytes32 kSepoliaGenesisHash{0x25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9_bytes32};
inline constexpr ChainConfig kSepoliaConfig{
    .chain_id = 11155111,
    .seal_engine = SealEngineType::kEthash,
    .homestead_block = 0,
    .tangerine_whistle_block = 0,
    .spurious_dragon_block = 0,
    .byzantium_block = 0,
    .constantinople_block = 0,
    .petersburg_block = 0,
    .istanbul_block = 0,
    .muir_glacier_block = 0,
    .berlin_block = 0,
    .london_block = 0,
    .terminal_total_difficulty = 17000000000000000,
    .merge_netsplit_block = 1735371,
};

//! \brief Looks up a known chain config provided its chain ID
std::optional<std::pair<const std::string, const ChainConfig*>> lookup_known_chain(uint64_t chain_id) noexcept;

//! \brief Looks up a known chain config provided its chain identifier (eg. "mainnet")
std::optional<std::pair<const std::string, const ChainConfig*>> lookup_known_chain(std::string_view identifier) noexcept;

//! \brief Returns a map known chains names mapped to their respective chain ids
std::map<std::string, uint64_t> get_known_chains_map() noexcept;

}  // namespace silkworm
