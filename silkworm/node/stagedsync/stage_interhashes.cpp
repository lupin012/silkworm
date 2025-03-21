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

#include "stage_interhashes.hpp"

#include <stdexcept>
#include <utility>

#include <absl/container/btree_set.h>

#include <silkworm/core/common/endian.hpp>
#include <silkworm/core/common/lru_cache.hpp>
#include <silkworm/core/trie/nibbles.hpp>
#include <silkworm/node/common/decoding_exception.hpp>
#include <silkworm/node/common/stopwatch.hpp>
#include <silkworm/node/db/access_layer.hpp>

namespace silkworm::stagedsync {

Stage::Result InterHashes::forward(db::RWTxn& txn) {
    Stage::Result ret{Stage::Result::kSuccess};
    operation_ = OperationType::Forward;

    try {
        throw_if_stopping();

        // Check stage boundaries from previous execution and previous stage execution
        auto previous_progress{get_progress(txn)};
        auto hashstate_stage_progress{db::stages::read_stage_progress(txn, db::stages::kHashStateKey)};
        if (previous_progress == hashstate_stage_progress) {
            // Nothing to process
            operation_ = OperationType::None;
            return Stage::Result::kSuccess;
        } else if (previous_progress > hashstate_stage_progress) {
            // Something bad had happened. Not possible hashstate stage is ahead of bodies
            // Maybe we need to unwind ?
            // Something bad had happened.  Maybe we need to unwind ?
            throw StageError(Stage::Result::kInvalidProgress,
                             "InterHashes progress " + std::to_string(previous_progress) +
                                 " greater than HashState progress " + std::to_string(hashstate_stage_progress));
        }

        BlockNum segment_width{hashstate_stage_progress - previous_progress};
        if (segment_width > db::stages::kSmallBlockSegmentWidth) {
            log::Info(log_prefix_ + " begin",
                      {"op", std::string(magic_enum::enum_name<OperationType>(operation_)),
                       "from", std::to_string(previous_progress),
                       "to", std::to_string(hashstate_stage_progress),
                       "span", std::to_string(segment_width)});
        }

        // Retrieve header's state_root at target block to be compared with the one computed here
        auto header_hash{db::read_canonical_header_hash(txn, hashstate_stage_progress)};
        if (!header_hash.has_value()) {
            throw std::runtime_error("Could not find hash for canonical header " +
                                     std::to_string(hashstate_stage_progress));
        }
        auto header{db::read_header(txn, hashstate_stage_progress, header_hash->bytes)};
        if (!header_hash.has_value()) {
            throw std::runtime_error("Could not find canonical header number " +
                                     std::to_string(hashstate_stage_progress) +
                                     " hash " + to_hex(header_hash->bytes, true));
        }
        auto expected_state_root{header->state_root};

        reset_log_progress();
        if (!previous_progress || segment_width > db::stages::kLargeBlockSegmentWorthRegen) {
            // Full regeneration
            ret = regenerate_intermediate_hashes(txn, &expected_state_root);
        } else {
            // Incremental update
            ret = increment_intermediate_hashes(txn, previous_progress, hashstate_stage_progress, &expected_state_root);
        }

        if (ret == Stage::Result::kWrongStateRoot) {
            // Binary search for the correct block, biased to the lower numbers
            sync_context_->unwind_point.emplace(previous_progress + (segment_width / 2));
            sync_context_->bad_block_hash.emplace(header_hash.value());
        }

        success_or_throw(ret);
        throw_if_stopping();
        db::stages::write_stage_progress(txn, db::stages::kIntermediateHashesKey, hashstate_stage_progress);
        txn.commit();

    } catch (const StageError& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = static_cast<Stage::Result>(ex.err());
    } catch (const mdbx::exception& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = Stage::Result::kDbError;
    } catch (const std::exception& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = Stage::Result::kUnexpectedError;
    } catch (...) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", "unexpected and undefined"});
        ret = Stage::Result::kUnexpectedError;
    }

    operation_ = OperationType::None;
    return ret;
}

Stage::Result InterHashes::unwind(db::RWTxn& txn) {
    Stage::Result ret{Stage::Result::kSuccess};

    if (!sync_context_->unwind_point.has_value()) return ret;
    const BlockNum to{sync_context_->unwind_point.value()};

    operation_ = OperationType::Unwind;

    try {
        throw_if_stopping();
        BlockNum previous_progress{get_progress(txn)};
        if (to >= previous_progress) {
            // Actually nothing to unwind
            operation_ = OperationType::None;
            return Stage::Result::kSuccess;
        }

        BlockNum segment_width{previous_progress - to};
        if (segment_width > db::stages::kSmallBlockSegmentWidth) {
            log::Info(log_prefix_ + " begin",
                      {"op", std::string(magic_enum::enum_name<OperationType>(operation_)),
                       "from", std::to_string(previous_progress),
                       "to", std::to_string(to),
                       "span", std::to_string(segment_width)});
        }

        // Retrieve header's state_root at target block to be compared with the one computed here
        // Retrieve header's state_root at target block to be compared with the one computed here
        auto header_hash{db::read_canonical_header_hash(txn, to)};
        if (!header_hash.has_value()) {
            throw std::runtime_error("Could not find hash for canonical header " +
                                     std::to_string(to));
        }
        auto header{db::read_header(txn, to, header_hash->bytes)};
        if (!header_hash.has_value()) {
            throw std::runtime_error("Could not find canonical header number " +
                                     std::to_string(to) +
                                     " hash " + to_hex(header_hash->bytes, true));
        }
        auto expected_state_root{header->state_root};

        reset_log_progress();
        if (segment_width > db::stages::kLargeBlockSegmentWorthRegen) {
            // Full regeneration
            // It will process all HashedState which is already unwound
            ret = regenerate_intermediate_hashes(txn, &expected_state_root);
        } else {
            // Incremental update
            ret = increment_intermediate_hashes(txn, previous_progress, to, &expected_state_root);
        }

        success_or_throw(ret);
        throw_if_stopping();
        db::stages::write_stage_progress(txn, db::stages::kIntermediateHashesKey, to);
        txn.commit();

    } catch (const StageError& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = static_cast<Stage::Result>(ex.err());
    } catch (const mdbx::exception& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = Stage::Result::kDbError;
    } catch (const std::exception& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = Stage::Result::kUnexpectedError;
    } catch (...) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", "unexpected and undefined"});
        ret = Stage::Result::kUnexpectedError;
    }

    operation_ = OperationType::None;
    return ret;
}

Stage::Result InterHashes::prune(db::RWTxn&) { return Stage::Result::kSuccess; }

trie::PrefixSet InterHashes::collect_account_changes(db::RWTxn& txn, BlockNum from, BlockNum to,
                                                     absl::btree_map<evmc::address, ethash_hash256>& hashed_addresses) {
    std::unique_ptr<StopWatch> sw;
    if (log::test_verbosity(log::Level::kTrace)) {
        sw = std::make_unique<StopWatch>(/*auto_start=*/true);
    }

    bool forward{to > from};  // Are we forwarding or unwinding ?

    BlockNum reached_blocknum{0};
    BlockNum expected_blocknum{std::min(from, to) + 1u};
    BlockNum max_blocknum{std::max(from, to)};

    absl::btree_set<Bytes> deleted_ts_prefixes{};
    silkworm::lru_cache<evmc::address, std::optional<Account>> plainstate_accounts(100'000);

    using namespace std::chrono_literals;
    auto log_time{std::chrono::steady_clock::now()};

    std::unique_lock log_lck(log_mtx_);
    current_source_ = std::string(db::table::kAccountChangeSet.name);
    log_lck.unlock();

    const Bytes starting_key{db::block_key(expected_blocknum)};
    trie::PrefixSet ret;

    db::PooledCursor account_changeset(txn, db::table::kAccountChangeSet);
    db::PooledCursor plain_state(txn, db::table::kPlainState);

    auto changeset_data{account_changeset.lower_bound(db::to_slice(starting_key), /*throw_notfound=*/false)};

    while (changeset_data) {
        reached_blocknum = endian::load_big_u64(db::from_slice(changeset_data.key).data());
        check_block_sequence(reached_blocknum, expected_blocknum);
        if (reached_blocknum > max_blocknum) {
            break;
        } else if (auto now{std::chrono::steady_clock::now()}; log_time <= now) {
            throw_if_stopping();
            log_lck.lock();
            log_time = now + 5s;
            current_key_ = std::to_string(reached_blocknum);
            log_lck.unlock();
        }

        while (changeset_data) {
            auto changeset_value_view{db::from_slice(changeset_data.value)};

            // Extract address and hash if needed
            const evmc::address address{to_evmc_address(changeset_value_view)};
            changeset_value_view.remove_prefix(kAddressLength);
            auto hashed_addresses_it{hashed_addresses.find(address)};
            if (hashed_addresses_it == hashed_addresses.end()) {
                const auto hashed_address{keccak256(address)};
                hashed_addresses_it = hashed_addresses.insert_or_assign(address, hashed_address).first;
            }

            // Lookup value in plainstate if any
            // Note ! on unwinds plainstate has not been unwound yet.
            std::optional<Account> plainstate_account{};
            if (auto item{plainstate_accounts.get(address)}; item != nullptr) {
                plainstate_account = *item;
            } else {
                auto ps_data{plain_state.find(db::to_slice(address.bytes), false)};
                if (ps_data && ps_data.value.length()) {
                    const auto account{Account::from_encoded_storage(db::from_slice(ps_data.value))};
                    success_or_throw(account);
                    plainstate_account.emplace(*account);
                }
                plainstate_accounts.put(address, plainstate_account);
            }

            bool account_created{false};  // Whether the account has to be marked as created in changed list

            if (forward) {
                // For forward collection:
                // Creation : if there is no value in changeset it means the account has been created
                // TrieStorage cleanup : if there is value in changeset we check account in changeset matches account in
                // plainstate Specifically if both have value and incarnations do not match then a self-destruct has
                // happened (with possible recreation). If they don't match delete from TrieStorage all hashed addresses
                // + incarnation
                if (!changeset_value_view.empty()) {
                    const auto changeset_account{Account::from_encoded_storage(changeset_value_view)};
                    success_or_throw(changeset_account);
                    if (changeset_account->incarnation) {
                        if (plainstate_account == std::nullopt ||
                            plainstate_account->incarnation != changeset_account->incarnation) {
                            deleted_ts_prefixes.insert(
                                db::storage_prefix(address.bytes, changeset_account->incarnation));
                        }
                    }
                } else {
                    account_created = true;
                }
            } else {
                // For unwind collection:
                // Creation : if there is no value in plainstate then it means the account has been created
                if (plainstate_account != std::nullopt) {
                    if (plainstate_account->incarnation) {
                        if (changeset_value_view.empty()) {
                            deleted_ts_prefixes.insert(address.bytes);
                        } else {
                            const auto changeset_account{Account::from_encoded_storage(changeset_value_view)};
                            success_or_throw(changeset_account);
                            if (changeset_account->incarnation > plainstate_account->incarnation) {
                                deleted_ts_prefixes.insert(
                                    db::storage_prefix(address.bytes, plainstate_account->incarnation));
                            }
                        }
                    }
                } else {
                    account_created = true;
                }
            }

            ret.insert(trie::unpack_nibbles(hashed_addresses_it->second.bytes), account_created);
            changeset_data = account_changeset.to_current_next_multi(/*throw_notfound=*/false);
        }

        ++expected_blocknum;
        changeset_data = account_changeset.to_next(/*throw_notfound=*/false);
    }

    // Eventually delete nodes from trie for deleted accounts
    if (!deleted_ts_prefixes.empty()) {
        db::PooledCursor trie_storage(txn, db::table::kTrieOfStorage);
        for (const auto& prefix : deleted_ts_prefixes) {
            const auto prefix_slice{db::to_slice(prefix)};
            auto data{trie_storage.lower_bound(prefix_slice, /*throw_notfound=*/false)};
            while (data && data.key.starts_with(prefix_slice)) {
                trie_storage.erase();
                data = trie_storage.to_next(/*throw_notfound=*/false);
            }
        }
    }

    if (sw) {
        const auto [_, duration]{sw->stop()};
        log::Trace(log_prefix_ + " gathered account changes", {"in", StopWatch::format(duration)});
    }
    return ret;
}

trie::PrefixSet InterHashes::collect_storage_changes(db::RWTxn& txn, BlockNum from, BlockNum to,
                                                     absl::btree_map<evmc::address, ethash_hash256>& hashed_addresses) {
    std::unique_ptr<StopWatch> sw;
    if (log::test_verbosity(log::Level::kTrace)) {
        sw = std::make_unique<StopWatch>(/*auto_start=*/true);
    }

    BlockNum reached_blocknum{0};
    BlockNum expected_blocknum{from + 1};

    using namespace std::chrono_literals;
    auto log_time{std::chrono::steady_clock::now()};

    std::unique_lock log_lck(log_mtx_);
    current_source_ = std::string(db::table::kStorageChangeSet.name);
    current_key_ = std::to_string(expected_blocknum);
    log_lck.unlock();

    const Bytes starting_key{db::block_key(expected_blocknum)};
    trie::PrefixSet ret;

    // Don't rehash same addresses
    absl::btree_map<evmc::address, ethash_hash256>::iterator hashed_addresses_it{hashed_addresses.begin()};

    db::PooledCursor storage_changeset(txn, db::table::kStorageChangeSet);
    auto changeset_data{storage_changeset.lower_bound(db::to_slice(starting_key), /*throw_notfound=*/false)};

    while (changeset_data) {
        auto changeset_key_view{db::from_slice(changeset_data.key)};
        reached_blocknum = endian::load_big_u64(changeset_key_view.data());

        if (reached_blocknum > to) {
            break;
        } else if (auto now{std::chrono::steady_clock::now()}; log_time <= now) {
            throw_if_stopping();
            log_lck.lock();
            log_time = now + 5s;
            current_key_ = std::to_string(reached_blocknum);
            log_lck.unlock();
        }

        changeset_key_view.remove_prefix(sizeof(BlockNum));

        const evmc::address address{to_evmc_address(changeset_key_view)};
        hashed_addresses_it = hashed_addresses.find(address);
        if (hashed_addresses_it == hashed_addresses.end()) {
            const auto hashed_address{keccak256(address.bytes)};
            hashed_addresses_it = hashed_addresses.insert_or_assign(address, hashed_address).first;
        }

        changeset_key_view.remove_prefix(kAddressLength);

        Bytes hashed_key(db::kHashedStoragePrefixLength + (2 * kHashLength), '\0');
        std::memcpy(&hashed_key[0], hashed_addresses_it->second.bytes, kHashLength);
        std::memcpy(&hashed_key[kHashLength], changeset_key_view.data(), db::kIncarnationLength);

        while (changeset_data) {
            auto changeset_value_view{db::from_slice(changeset_data.value)};

            const ByteView location{changeset_value_view.substr(0, kHashLength)};
            const auto hashed_location{keccak256(location)};

            auto unpacked_location{trie::unpack_nibbles(hashed_location.bytes)};
            std::memcpy(&hashed_key[db::kHashedStoragePrefixLength], unpacked_location.data(),
                        unpacked_location.length());
            auto ret_item{ByteView(hashed_key.data(), db::kHashedStoragePrefixLength + unpacked_location.length())};

            ret.insert(ret_item, changeset_value_view.length() == kHashLength);
            changeset_data = storage_changeset.to_current_next_multi(/*throw_notfound=*/false);
        }

        changeset_data = storage_changeset.to_next(/*throw_notfound=*/false);
    }

    if (sw) {
        const auto [_, duration]{sw->stop()};
        log::Trace(log_prefix_ + " gathered storage changes", {"in", StopWatch::format(duration)});
    }

    return ret;
}

Stage::Result InterHashes::regenerate_intermediate_hashes(db::RWTxn& txn, const evmc::bytes32* expected_root) {
    std::unique_lock log_lck(log_mtx_);
    incremental_ = true;
    current_source_.clear();
    current_target_.clear();
    log_lck.unlock();
    Stage::Result ret{Stage::Result::kSuccess};

    try {
        log::Info(log_prefix_, {"clearing", db::table::kTrieOfAccounts.name});
        txn->clear_map(db::table::kTrieOfAccounts.name);
        log::Info(log_prefix_, {"clearing", db::table::kTrieOfStorage.name});
        txn->clear_map(db::table::kTrieOfStorage.name);
        txn.commit();

        account_collector_ = std::make_unique<etl::Collector>(node_settings_);
        storage_collector_ = std::make_unique<etl::Collector>(node_settings_);

        log_lck.lock();
        current_source_ = "HashState";
        current_target_.clear();
        current_key_.clear();
        trie_loader_ = std::make_unique<trie::TrieLoader>(*txn, nullptr, nullptr, account_collector_.get(),
                                                          storage_collector_.get());
        log_lck.unlock();

        const evmc::bytes32 computed_root{trie_loader_->calculate_root()};

        // Fail if not what expected
        if (expected_root != nullptr && computed_root != *expected_root) {
            log_lck.lock();
            trie_loader_.reset();        // Don't need anymore
            account_collector_.reset();  // Will invoke dtor which causes all flushed files (if any) to be deleted
            storage_collector_.reset();  // Will invoke dtor which causes all flushed files (if any) to be deleted
            log_lck.unlock();
            const std::string what{"expected " + to_hex(*expected_root, true) + " got " + to_hex(computed_root, true)};
            throw StageError(Stage::Result::kWrongStateRoot, what);
        }

        flush_collected_nodes(txn);

    } catch (const StageError& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = static_cast<Stage::Result>(ex.err());
    } catch (const mdbx::exception& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = Stage::Result::kDbError;
    } catch (const std::exception& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = Stage::Result::kUnexpectedError;
    } catch (...) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", "unexpected and undefined"});
        ret = Stage::Result::kUnexpectedError;
    }

    return ret;
}

Stage::Result InterHashes::increment_intermediate_hashes(db::RWTxn& txn, BlockNum from, BlockNum to,
                                                         const evmc::bytes32* expected_root) {
    std::unique_lock log_lck(log_mtx_);
    incremental_ = true;
    current_source_ = "ChangeSets";
    log_lck.unlock();
    Stage::Result ret{Stage::Result::kSuccess};

    try {
        account_collector_ = std::make_unique<etl::Collector>(node_settings_);
        storage_collector_ = std::make_unique<etl::Collector>(node_settings_);

        // Cache of hashed addresses
        absl::btree_map<evmc::address, ethash_hash256> hashed_addresses{};
        // Collect all changes from changesets
        trie::PrefixSet account_changes{collect_account_changes(txn, from, to, hashed_addresses)};
        trie::PrefixSet storage_changes{collect_storage_changes(txn, from, to, hashed_addresses)};
        // Remove unneeded RAM occupation
        hashed_addresses.clear();

        log_lck.lock();
        current_source_ = "ChangeSets";
        current_target_.clear();
        current_key_.clear();
        trie_loader_ = std::make_unique<trie::TrieLoader>(*txn, &account_changes, &storage_changes,
                                                          account_collector_.get(), storage_collector_.get());
        log_lck.unlock();

        const evmc::bytes32 computed_root{trie_loader_->calculate_root()};

        // Fail if not what expected
        if (expected_root != nullptr && computed_root != *expected_root) {
            log_lck.lock();
            trie_loader_.reset();        // Don't need anymore
            account_collector_.reset();  // Will invoke dtor which causes all flushed files (if any) to be deleted
            storage_collector_.reset();  // Will invoke dtor which causes all flushed files (if any) to be deleted
            log_lck.unlock();
            log::Error("Wrong trie root",
                       {"expected", to_hex(*expected_root, true), "got", to_hex(computed_root, true)});
            return Stage::Result::kWrongStateRoot;
        }

        flush_collected_nodes(txn);

    } catch (const StageError& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = static_cast<Stage::Result>(ex.err());
    } catch (const mdbx::exception& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = Stage::Result::kDbError;
    } catch (const std::exception& ex) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", std::string(ex.what())});
        ret = Stage::Result::kUnexpectedError;
    } catch (...) {
        log::Error(log_prefix_,
                   {"function", std::string(__FUNCTION__), "exception", "unexpected and undefined"});
        ret = Stage::Result::kUnexpectedError;
    }

    return ret;
}

void InterHashes::flush_collected_nodes(db::RWTxn& txn) {
    // Proceed with loading of newly generated nodes and deletion of obsolete ones.
    std::unique_lock log_lck(log_mtx_);
    trie_loader_.reset();
    loading_ = true;
    loading_collector_ = std::move(account_collector_);
    current_source_ = "etl";
    current_target_ = std::string(db::table::kTrieOfAccounts.name);
    log_lck.unlock();

    db::PooledCursor target(txn, db::table::kTrieOfAccounts);
    MDBX_put_flags_t flags{target.empty() ? MDBX_put_flags_t::MDBX_APPEND : MDBX_put_flags_t::MDBX_UPSERT};
    loading_collector_->load(target, nullptr, flags);

    log_lck.lock();
    loading_collector_ = std::move(storage_collector_);
    current_target_ = std::string(db::table::kTrieOfStorage.name);
    log_lck.unlock();

    target.bind(txn, db::table::kTrieOfStorage);
    flags = target.empty() ? MDBX_put_flags_t::MDBX_APPEND : MDBX_put_flags_t::MDBX_UPSERT;
    loading_collector_->load(target, nullptr, flags);

    log_lck.lock();
    current_source_.clear();
    current_target_.clear();
    loading_ = false;
    loading_collector_.reset();
    log_lck.unlock();
}

void InterHashes::reset_log_progress() {
    std::unique_lock log_lck(log_mtx_);
    current_source_.clear();
    current_target_.clear();
    current_key_.clear();
}

std::vector<std::string> InterHashes::get_log_progress() {
    std::unique_lock log_lck(log_mtx_);
    std::vector<std::string> ret{"op", std::string(magic_enum::enum_name<OperationType>(operation_)),
                                 "mode", (incremental_ ? "incr" : "full")};

    if (trie_loader_) {
        current_key_ = abridge(trie_loader_->get_log_key(), kAddressLength);
        ret.insert(ret.end(), {"op", "building merkle tree", "key", current_key_});
    } else {
        if (current_source_.empty() && current_target_.empty()) {
            ret.insert(ret.end(), {"db", "waiting ..."});
        } else {
            if (loading_) {
                ret.insert(ret.end(), {"from", "etl", "to", current_target_});
                if (loading_collector_) {
                    current_key_ = abridge(loading_collector_->get_load_key(), kHashLength);
                    ret.insert(ret.end(), {"key", current_key_});
                }
            } else {
                ret.insert(ret.end(), {"from", current_source_, "key", current_key_});
            }
        }
    }
    return ret;
}
}  // namespace silkworm::stagedsync
