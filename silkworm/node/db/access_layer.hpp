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

// Database Access Layer
// See Erigon core/rawdb/accessors_chain.go

#include <optional>
#include <span>
#include <string>
#include <vector>

#include <silkworm/core/chain/config.hpp>
#include <silkworm/core/types/account.hpp>
#include <silkworm/core/types/block.hpp>
#include <silkworm/node/db/mdbx.hpp>
#include <silkworm/node/db/util.hpp>

namespace silkworm::db {

//! \brief Pulls database schema version
std::optional<VersionBase> read_schema_version(ROTxn& txn);

//! \brief Writes database schema version (throws on downgrade)
void write_schema_version(RWTxn& txn, const VersionBase& schema_version);

//! \brief Updates database info with build info at provided height
//! \details Is useful to track whether increasing heights have been affected by
//! upgrades or downgrades of Silkworm's build
void write_build_info_height(RWTxn& txn, Bytes key, BlockNum height);

//! \brief Read the list of snapshot file names
std::vector<std::string> read_snapshots(ROTxn& txn);

//! \brief Write the list of snapshot file names
void write_snapshots(RWTxn& txn, const std::vector<std::string>& snapshot_file_names);

//! \brief Reads a header with the specified key (block number, hash)
std::optional<BlockHeader> read_header(ROTxn& txn, BlockNum block_number, const uint8_t (&hash)[kHashLength]);
std::optional<BlockHeader> read_header(ROTxn& txn, BlockNum block_number, const evmc::bytes32&);
std::optional<BlockHeader> read_header(ROTxn& txn, ByteView key);
Bytes read_header_raw(ROTxn& txn, ByteView key);

//! \brief Reads a header with the specified hash
std::optional<BlockHeader> read_header(ROTxn& txn, const evmc::bytes32& hash);

//! \brief Reads all headers at the specified height
std::vector<BlockHeader> read_headers(ROTxn& txn, BlockNum height);

//! \brief Apply a user defined func to the headers at specific height
size_t process_headers_at_height(ROTxn& txn, BlockNum height, std::function<void(BlockHeader&&)> process_func);

//! \brief Reads a header without rlp-decoding it
std::optional<ByteView> read_rlp_encoded_header(ROTxn& txn, BlockNum bn, const evmc::bytes32& hash);

//! \brief Reads the canonical head
std::tuple<BlockNum, evmc::bytes32> read_canonical_head(ROTxn& txn);

//! \brief Reads the canonical header from a block number
std::optional<BlockHeader> read_canonical_header(ROTxn& txn, BlockNum b);

//! \brief Writes given header to table::kHeaders
void write_header(RWTxn& txn, const BlockHeader& header, bool with_header_numbers = false);

//! \brief Read block number from hash
std::optional<BlockNum> read_block_number(ROTxn& txn, const evmc::bytes32& hash);

//! \brief Writes header hash in table::kHeaderNumbers
void write_header_number(RWTxn& txn, const uint8_t (&hash)[kHashLength], BlockNum number);

//! \brief Writes the header hash in table::kCanonicalHashes
void write_canonical_header(RWTxn& txn, const BlockHeader& header);

//! \brief Reads the header hash in table::kCanonicalHashes
std::optional<evmc::bytes32> read_canonical_header_hash(ROTxn& txn, BlockNum number);

//! \brief Writes the header hash in table::kCanonicalHashes
void write_canonical_header_hash(RWTxn& txn, const uint8_t (&hash)[kHashLength], BlockNum number);

//! \brief Read a block body (in an out parameter) returning true on success and false on missing block
[[nodiscard]] bool read_body(ROTxn& txn, const Bytes& key, bool read_senders, BlockBody& out);
[[nodiscard]] bool read_body(ROTxn& txn, BlockNum block_number, const uint8_t (&hash)[kHashLength],
                             bool read_senders, BlockBody& out);
[[nodiscard]] bool read_body(ROTxn& txn, const evmc::bytes32& hash, BlockNum bn, BlockBody& body);
[[nodiscard]] bool read_body(ROTxn& txn, const evmc::bytes32& hash, BlockBody& body);

//! \brief Read the canonical block at specified height
[[nodiscard]] bool read_canonical_block(ROTxn& txn, BlockNum height, Block& block);

//! \brief Apply a user defined func to the bodies at specific height
size_t process_blocks_at_height(ROTxn& txn, BlockNum height, std::function<void(Block&)> process_func,
                                bool read_senders = false);

//! \brief Check the presence of a block body using block number and hash
[[nodiscard]] bool has_body(ROTxn& txn, BlockNum block_number, const uint8_t (&hash)[kHashLength]);
[[nodiscard]] bool has_body(ROTxn& txn, BlockNum block_number, const evmc::bytes32& hash);

//! \brief Writes block body in table::kBlockBodies
void write_body(RWTxn& txn, const BlockBody& body, const evmc::bytes32& hash, BlockNum bn);
void write_body(RWTxn& txn, const BlockBody& body, const uint8_t (&hash)[kHashLength], BlockNum number);

// See Erigon ReadTd
std::optional<intx::uint256> read_total_difficulty(ROTxn& txn, BlockNum, const evmc::bytes32& hash);
std::optional<intx::uint256> read_total_difficulty(ROTxn& txn, BlockNum, const uint8_t (&hash)[kHashLength]);
std::optional<intx::uint256> read_total_difficulty(ROTxn& txn, ByteView key);

// See Erigon WriteTd
void write_total_difficulty(RWTxn& txn, BlockNum, const evmc::bytes32& hash, const intx::uint256& total_difficulty);
void write_total_difficulty(RWTxn& txn, BlockNum, const uint8_t (&hash)[kHashLength], const intx::uint256& td);
void write_total_difficulty(RWTxn& txn, const Bytes& key, const intx::uint256& total_difficulty);

// Reads canonical block; see Erigon ReadBlockByNumber.
// Returns true on success and false on missing block.
[[nodiscard]] bool read_block_by_number(ROTxn& txn, BlockNum number, bool read_senders, Block& out);

// Reads a block; see Erigon ReadBlock.
// Returns true on success and false on missing block.
[[nodiscard]] bool read_block(ROTxn& txn, std::span<const uint8_t, kHashLength> hash, BlockNum number,
                              bool read_senders, Block& out);
[[nodiscard]] bool read_block(ROTxn& txn, const evmc::bytes32& hash, BlockNum number, Block& block);

// See Erigon ReadSenders
std::vector<evmc::address> read_senders(ROTxn& txn, const Bytes& key);
std::vector<evmc::address> read_senders(ROTxn& txn, BlockNum block_number, const uint8_t (&hash)[kHashLength]);
//! \brief Fills transactions' senders addresses directly in place
void parse_senders(ROTxn& txn, const Bytes& key, std::vector<Transaction>& out);

// See Erigon ReadTransactions
void read_transactions(ROTxn& txn, uint64_t base_id, uint64_t count, std::vector<Transaction>& out);
void read_transactions(mdbx::cursor& txn_table, uint64_t base_id, uint64_t count, std::vector<Transaction>& out);

//! \brief Persist transactions into db's bucket table::kBlockTransactions.
//! The key starts from base_id and is incremented by 1 for each transaction.
//! \remarks Before calling this ensure you got a proper base_id by incrementing sequence for table::kBlockTransactions.
void write_transactions(RWTxn& txn, const std::vector<Transaction>& transactions, uint64_t base_id);

std::optional<ByteView> read_code(ROTxn& txn, const evmc::bytes32& code_hash);

// Reads current or historical (if block_number is specified) account.
std::optional<Account> read_account(ROTxn& txn, const evmc::address& address,
                                    std::optional<BlockNum> block_number = std::nullopt);

// Reads current or historical (if block_number is specified) storage.
evmc::bytes32 read_storage(ROTxn& txn, const evmc::address& address, uint64_t incarnation,
                           const evmc::bytes32& location, std::optional<BlockNum> block_number = std::nullopt);

// Reads current or historical (if block_number is specified) previous incarnation.
std::optional<uint64_t> read_previous_incarnation(ROTxn& txn, const evmc::address& address,
                                                  std::optional<BlockNum> block_number = std::nullopt);

AccountChanges read_account_changes(ROTxn& txn, BlockNum block_number);

StorageChanges read_storage_changes(ROTxn& txn, BlockNum block_number);

//! \brief Retrieves the chain_id for which database is populated
//! \see Erigon chainConfig / chainConfigWithGenesis
std::optional<ChainConfig> read_chain_config(ROTxn& txn);

//! \brief Writes / Updates chain config provided genesis has been initialized
void update_chain_config(RWTxn& txn, const ChainConfig& config);

//! \brief Updates highest header hash in table::kHeadHeader
void write_head_header_hash(RWTxn& txn, const uint8_t (&hash)[kHashLength]);
void write_head_header_hash(RWTxn& txn, const evmc::bytes32& hash);

//! \brief Reads highest header hash from table::kHeadHeader
std::optional<evmc::bytes32> read_head_header_hash(ROTxn& txn);

//! \brief Reads canonical hash from block number
std::optional<evmc::bytes32> read_canonical_hash(ROTxn& txn, BlockNum b);

//! \brief Delete a canonical hash associated to a block number
void delete_canonical_hash(RWTxn& txn, BlockNum b);

//! \brief Write canonical hash
void write_canonical_hash(RWTxn& txn, BlockNum b, const evmc::bytes32& hash);

//! \brief Gets/Increments the sequence value for a given map (bucket)
//! \param [in] map_name : the name of the map to get a sequence for
//! \param [in] increment : the value of increments to add to the sequence.
//! \returns The current value of the sequence AND internally increments the value for next call
//! \throws std::std::length_error on badly recorded value
//! \remarks Initial sequence for any key (also unset) is 0. Changes to sequences are invisible until the transaction is
//! committed
uint64_t increment_map_sequence(RWTxn& txn, const char* map_name, uint64_t increment = 1u);

//! \brief Returns the current sequence for a map_name
//! \remarks If the key is not present in Sequence bucket the return value is 0
//! \throws std::std::length_error on badly recorded value
uint64_t read_map_sequence(ROTxn& txn, const char* map_name);

//! \brief Reset the sequence value for a given map (bucket)
//! \param [in] map_name : the name of the map to reset the sequence for
//! \param [in] new_sequence : the value to set the sequence to
//! \returns The old value of the sequence
//! \throws std::std::length_error on badly recorded value
//! \remarks Initial sequence for any key (also unset) is 0. Changes to sequences are invisible until the transaction is
//! committed
uint64_t reset_map_sequence(RWTxn& txn, const char* map_name, uint64_t new_sequence);

}  // namespace silkworm::db
