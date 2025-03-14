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

#include "execution_engine.hpp"

#include <set>

#include <silkworm/core/common/as_range.hpp>
#include <silkworm/node/db/access_layer.hpp>
#include <silkworm/node/db/db_utils.hpp>

namespace silkworm::stagedsync {

static void ensure_invariant(bool condition, const std::string& message) {
    if (!condition) {
        throw std::logic_error("Execution invariant violation: " + message);
    }
}

ExecutionEngine::CanonicalChain::CanonicalChain(db::RWTxn& tx) : tx_{tx}, canonical_cache_{kCacheSize} {
    // Read head of canonical chain
    std::tie(initial_head_.number, initial_head_.hash) = db::read_canonical_head(tx_);
    // Set current status
    current_head_ = initial_head_;
}

BlockId ExecutionEngine::CanonicalChain::initial_head() { return initial_head_; }
BlockId ExecutionEngine::CanonicalChain::current_head() { return current_head_; }

BlockNum ExecutionEngine::CanonicalChain::find_forking_point(db::RWTxn& tx, Hash header_hash) {
    BlockNum forking_point{};

    std::optional<BlockHeader> header = db::read_header(tx, header_hash);  // todo: maybe use parent cache?
    if (!header) throw std::logic_error("find_forking_point precondition violation, header not found");
    if (header->number == 0) return forking_point;

    BlockNum height = header->number;
    Hash parent_hash = header->parent_hash;

    // Read canonical hash at height-1
    auto prev_canon_hash = canonical_cache_.get_as_copy(height - 1);  // look in the cache first
    if (!prev_canon_hash) {
        prev_canon_hash = db::read_canonical_hash(tx, height - 1);  // then look in the db
    }

    // Most common case: forking point is the height of the parent header
    if (prev_canon_hash == header->parent_hash) {
        forking_point = height - 1;
    }
    // Going further back
    else {
        auto parent = db::read_header(tx, height - 1, parent_hash);  // todo: maybe use parent cache?
        ensure_invariant(parent.has_value(),
                         "canonical chain could not find parent with hash " + to_hex(parent_hash) +
                             " and height " + std::to_string(height - 1) + " for header " + to_hex(header->hash()));

        auto ancestor_hash = parent->parent_hash;
        auto ancestor_height = height - 2;

        // look in the cache first
        const Hash* cached_canon_hash;
        while ((cached_canon_hash = canonical_cache_.get(ancestor_height)) && *cached_canon_hash != ancestor_hash) {
            auto ancestor = db::read_header(tx, ancestor_height, ancestor_hash);  // todo: maybe use parent cache?
            ancestor_hash = ancestor->parent_hash;
            --ancestor_height;
        }  // if this loop finds a prev_canon_hash the next loop will be executed, is this right?

        // now look in the db
        std::optional<Hash> db_canon_hash;
        while ((db_canon_hash = read_canonical_hash(tx, ancestor_height)) && db_canon_hash != ancestor_hash) {
            auto ancestor = db::read_header(tx, ancestor_height, ancestor_hash);  // todo: maybe use parent cache?
            ancestor_hash = ancestor->parent_hash;
            --ancestor_height;
        }

        // loop above terminates when prev_canon_hash == ancestor_hash, therefore ancestor_height is our forking point
        forking_point = ancestor_height;
    }

    return forking_point;
}

void ExecutionEngine::CanonicalChain::update_up_to(BlockNum height, Hash hash) {  // hash can be empty
    if (height == 0) return;

    auto ancestor_hash = hash;
    auto ancestor_height = height;

    std::optional<Hash> persisted_canon_hash = db::read_canonical_hash(tx_, ancestor_height);
    // while (persisted_canon_hash != ancestor_hash) { // better but gcc12 release erroneously raises a maybe-uninitialized warn
    while (!persisted_canon_hash ||
           std::memcmp(persisted_canon_hash.value().bytes, ancestor_hash.bytes, kHashLength) != 0) {
        db::write_canonical_hash(tx_, ancestor_height, ancestor_hash);
        canonical_cache_.put(ancestor_height, ancestor_hash);

        auto ancestor = db::read_header(tx_, ancestor_height, ancestor_hash);  // todo: maybe use parent cache?
        ensure_invariant(ancestor.has_value(),
                         "fix canonical chain failed at ancestor= " + std::to_string(ancestor_height) +
                             " hash=" + ancestor_hash.to_hex());

        ancestor_hash = ancestor->parent_hash;
        --ancestor_height;

        persisted_canon_hash = db::read_canonical_hash(tx_, ancestor_height);
    }

    current_head_.number = height;
    current_head_.hash = hash;
}

void ExecutionEngine::CanonicalChain::delete_down_to(BlockNum unwind_point) {
    for (BlockNum current_height = current_head_.number; current_height > unwind_point; current_height--) {
        db::delete_canonical_hash(tx_, current_height);  // do not throw if not found
        canonical_cache_.remove(current_height);
    }

    current_head_.number = unwind_point;
    auto current_head_hash = db::read_canonical_hash(tx_, unwind_point);
    ensure_invariant(current_head_hash.has_value(), "hash not found on canonical at height " + std::to_string(unwind_point));

    current_head_.hash = *current_head_hash;
}

auto ExecutionEngine::CanonicalChain::get_hash(BlockNum height) -> std::optional<Hash> {
    return db::read_canonical_hash(tx_, height);
}

// --------------------------------------------------------------------------------------------------------------------

ExecutionEngine::ExecutionEngine(NodeSettings& ns, const db::RWAccess dba)
    : node_settings_{ns},
      db_access_{dba},
      tx_{db_access_.start_rw_tx()},
      pipeline_{&ns},
      canonical_chain_(tx_),
      canonical_status_{ValidChain{0}},  // we do not know the last status yet
      last_fork_choice_{0}
// header_cache_{kCacheSize}
{
}

auto ExecutionEngine::current_status() -> VerificationResult {
    return canonical_status_;
}

auto ExecutionEngine::last_fork_choice() -> BlockId {
    return last_fork_choice_;
}

void ExecutionEngine::insert_header(const BlockHeader& header) {
    // skip 'if (!db::has_header(...header.hash())' to avoid hash computing (also write_header does an upsert)
    db::write_header(tx_, header, true);  // todo: move?

    // header_cache_.put(header.hash(), header);
}

void ExecutionEngine::insert_body(const Block& block) {
    Hash block_hash = block.header.hash();  // todo: hash() is computationally expensive
    BlockNum block_num = block.header.number;

    if (!db::has_body(tx_, block_num, block_hash)) {
        db::write_body(tx_, block, block_hash, block_num);
    }
}

void ExecutionEngine::insert_block(const Block& block) {
    insert_header(block.header);
    insert_body(block);
}

auto ExecutionEngine::verify_chain(Hash head_block_hash) -> VerificationResult {
    SILK_TRACE << "ExecutionEngine: verifying chain " << head_block_hash.to_hex();

    // retrieve the head header
    auto header = get_header(head_block_hash);
    ensure_invariant(header.has_value(), "header to verify non present");

    // db commit policy
    bool commit_at_each_stage = is_first_sync_;
    if (!commit_at_each_stage) tx_.disable_commit();

    // the new head is on a new fork?
    BlockNum forking_point = canonical_chain_.find_forking_point(tx_, head_block_hash);  // the forking origin

    if (forking_point < canonical_chain_.current_head().number) {  // if the forking is behind the current head
        // we need to do unwind to change canonical
        auto unwind_result = pipeline_.unwind(tx_, forking_point);
        success_or_throw(unwind_result);  // unwind must complete with success
        // remove last part of canonical
        canonical_chain_.delete_down_to(forking_point);
    }

    // update canonical up to header_hash
    canonical_chain_.update_up_to(header->number, head_block_hash);

    // forward
    Stage::Result forward_result = pipeline_.forward(tx_, header->number);

    // evaluate result
    VerificationResult verify_result;
    switch (forward_result) {
        case Stage::Result::kSuccess: {
            if (pipeline_.head_header_number() != canonical_chain_.current_head().number ||
                pipeline_.head_header_hash() != canonical_chain_.current_head().hash) {
                throw std::logic_error("forward succeeded but pipeline head is not aligned with canonical head");
            }
            verify_result = ValidChain{pipeline_.head_header_number()};
            break;
        }
        case Stage::Result::kWrongFork:
        case Stage::Result::kInvalidBlock:
        case Stage::Result::kWrongStateRoot: {
            ensure_invariant(pipeline_.unwind_point().has_value(),
                             "unwind point from pipeline requested when forward fails");
            InvalidChain invalid_chain = {*pipeline_.unwind_point()};
            invalid_chain.unwind_head = *canonical_chain_.get_hash(*pipeline_.unwind_point());
            if (pipeline_.bad_block()) {
                invalid_chain.bad_block = pipeline_.bad_block();
                invalid_chain.bad_headers = collect_bad_headers(tx_, invalid_chain);
            }
            verify_result = invalid_chain;
            break;
        }
        case Stage::Result::kStoppedByEnv:
            verify_result = ValidChain{pipeline_.head_header_number()};
            break;
        default:
            verify_result = ValidationError{pipeline_.head_header_number()};
    }

    // finish
    canonical_status_ = verify_result;
    tx_.enable_commit();
    if (commit_at_each_stage) tx_.commit_and_renew();
    return verify_result;
}

bool ExecutionEngine::notify_fork_choice_update(Hash head_block_hash) {
    if (canonical_chain_.current_head().hash != head_block_hash) {
        // usually update_fork_choice must follow verify_chain with the same header
        // except when verify_chain returned InvalidChain, in which case we expect
        // update_fork_choice to be called with a previous valid head block hash

        auto verification = verify_chain(head_block_hash);

        if (!std::holds_alternative<ValidChain>(verification)) return false;

        ensure_invariant(canonical_chain_.current_head().hash == head_block_hash,
                         "canonical head not aligned with fork choice");
    }

    tx_.commit_and_renew();

    last_fork_choice_ = canonical_chain_.current_head();

    is_first_sync_ = false;

    return true;
}

std::set<Hash> ExecutionEngine::collect_bad_headers(db::RWTxn& tx, InvalidChain& invalid_chain) {
    if (!invalid_chain.bad_block) return {};

    std::set<Hash> bad_headers;
    for (BlockNum current_height = canonical_chain_.current_head().number;
         current_height > invalid_chain.unwind_point; current_height--) {
        auto current_hash = db::read_canonical_hash(tx, current_height);
        bad_headers.insert(*current_hash);
    }

    /*  todo: check if we need also the following code (remember that this entire algo changed in Erigon)
    BlockNum new_height = unwind_point;
    if (is_bad_block) {
        bad_headers.insert(*bad_block);

        auto [max_block_num, max_hash] = header_with_biggest_td(tx, &bad_headers);

        if (max_block_num == 0) {
            max_block_num = unwind_point;
            max_hash = *db::read_canonical_hash(tx, max_block_num);
        }

        db::write_head_header_hash(tx, max_hash);
        new_height = max_block_num;
    }
    return {bad_headers, new_height};
    */
    return bad_headers;
}

auto ExecutionEngine::get_header(Hash header_hash) -> std::optional<BlockHeader> {
    // const BlockHeader* cached = header_cache_.get(header_hash);
    // if (cached) {
    //     return *cached;
    // }
    std::optional<BlockHeader> header = db::read_header(tx_, header_hash);
    return header;
}

auto ExecutionEngine::get_header(BlockNum header_height, Hash header_hash) -> std::optional<BlockHeader> {
    // const BlockHeader* cached = header_cache_.get(header_hash);
    // if (cached) {
    //     return *cached;
    // }
    std::optional<BlockHeader> header = db::read_header(tx_, header_height, header_hash);
    return header;
}

auto ExecutionEngine::get_canonical_hash(BlockNum height) -> std::optional<Hash> {
    auto hash = db::read_canonical_hash(tx_, height);
    return hash;
}

auto ExecutionEngine::get_header_td(BlockNum header_height, Hash header_hash) -> std::optional<TotalDifficulty> {
    return db::read_total_difficulty(tx_, header_height, header_hash);
}

auto ExecutionEngine::get_body(Hash header_hash) -> std::optional<BlockBody> {
    BlockBody body;
    bool found = read_body(tx_, header_hash, body);
    if (!found) return {};
    return body;
}

auto ExecutionEngine::get_block_progress() -> BlockNum {
    BlockNum block_progress = 0;

    read_headers_in_reverse_order(tx_, 1, [&block_progress](BlockHeader&& header) {
        block_progress = header.number;
    });

    return block_progress;
}

auto ExecutionEngine::get_canonical_head() -> ChainHead {
    auto [height, hash] = db::read_canonical_head(tx_);

    std::optional<TotalDifficulty> td = db::read_total_difficulty(tx_, height, hash);
    ensure_invariant(td.has_value(),
                     "total difficulty of canonical hash at height " + std::to_string(height) + " not found in db");

    return {height, hash, *td};
}

auto ExecutionEngine::get_last_headers(BlockNum limit) -> std::vector<BlockHeader> {
    std::vector<BlockHeader> headers;

    read_headers_in_reverse_order(tx_, limit, [&headers](BlockHeader&& header) {
        headers.emplace_back(std::move(header));
    });

    return headers;
}

auto ExecutionEngine::extends_last_fork_choice(BlockNum height, Hash hash) -> bool {
    while (height > last_fork_choice_.number) {
        auto header = get_header(height, hash);
        if (!header) return false;
        if (header->parent_hash == last_fork_choice_.hash) return true;
        height--;
        hash = header->parent_hash;
    }
    if (height == last_fork_choice_.number) return hash == last_fork_choice_.hash;

    return false;
}

}  // namespace silkworm::stagedsync
