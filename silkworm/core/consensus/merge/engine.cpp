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

#include "engine.hpp"

#include <optional>
#include <utility>

namespace silkworm::consensus {

MergeEngine::MergeEngine(std::unique_ptr<IEngine> eth1_engine, const ChainConfig& chain_config)
    : terminal_total_difficulty_{*chain_config.terminal_total_difficulty},
      pre_merge_engine_{std::move(eth1_engine)},
      post_merge_engine_{chain_config} {}

ValidationResult MergeEngine::pre_validate_block_body(const Block& block, const BlockState& state) {
    if (block.header.difficulty != 0) {
        return pre_merge_engine_->pre_validate_block_body(block, state);
    } else {
        return post_merge_engine_.pre_validate_block_body(block, state);
    }
}

ValidationResult MergeEngine::validate_block_header(const BlockHeader& header, const BlockState& state,
                                                    bool with_future_timestamp_check) {
    // TODO (Andrew) how will all this work with backwards sync?

    const std::optional<BlockHeader> parent{EngineBase::get_parent_header(state, header)};
    if (!parent.has_value()) {
        return ValidationResult::kUnknownParent;
    }

    if (header.difficulty != 0) {
        const std::optional<intx::uint256> parent_total_difficulty{
            state.total_difficulty(parent->number, header.parent_hash)};
        if (parent_total_difficulty == std::nullopt) {
            return ValidationResult::kUnknownParentTotalDifficulty;
        }
        if (parent_total_difficulty >= terminal_total_difficulty_) {
            return ValidationResult::kPoWBlockAfterMerge;
        }
        return pre_merge_engine_->validate_block_header(header, state, with_future_timestamp_check);
    } else {
        if (parent->difficulty != 0 && !terminal_pow_block(*parent, state)) {
            return ValidationResult::kPoSBlockBeforeMerge;
        }
        return post_merge_engine_.validate_block_header(header, state, with_future_timestamp_check);
    }
}

bool MergeEngine::terminal_pow_block(const BlockHeader& header, const BlockState& state) const {
    if (header.difficulty == 0) {
        return false;  // PoS block
    }

    const std::optional<BlockHeader> parent{EngineBase::get_parent_header(state, header)};
    if (parent == std::nullopt) {
        return false;
    }

    const std::optional<intx::uint256> parent_total_difficulty{
        state.total_difficulty(parent->number, header.parent_hash)};
    if (parent_total_difficulty == std::nullopt) {
        // TODO (Andrew) should return kUnknownParentTotalDifficulty instead
        return false;
    }

    return parent_total_difficulty < terminal_total_difficulty_ &&
           *parent_total_difficulty + header.difficulty >= terminal_total_difficulty_;
}

ValidationResult MergeEngine::validate_seal(const BlockHeader& header) {
    if (header.difficulty != 0) {
        return pre_merge_engine_->validate_seal(header);
    } else {
        return post_merge_engine_.validate_seal(header);
    }
}

void MergeEngine::finalize(IntraBlockState& state, const Block& block, evmc_revision revision) {
    if (block.header.difficulty != 0) {
        pre_merge_engine_->finalize(state, block, revision);
    } else {
        post_merge_engine_.finalize(state, block, revision);
    }
}

evmc::address MergeEngine::get_beneficiary(const BlockHeader& header) {
    if (header.difficulty != 0) {
        return pre_merge_engine_->get_beneficiary(header);
    } else {
        return post_merge_engine_.get_beneficiary(header);
    }
}

ValidationResult MergeEngine::validate_ommers(const Block& block, const BlockState& state) {
    if (block.header.difficulty != 0) {
        return pre_merge_engine_->validate_ommers(block, state);
    } else {
        return post_merge_engine_.validate_ommers(block, state);
    }
}

ValidationResult MergeEngine::pre_validate_transactions(const Block& block) {
    if (block.header.difficulty != 0) {
        return pre_merge_engine_->pre_validate_transactions(block);
    } else {
        return post_merge_engine_.pre_validate_transactions(block);
    }
}

}  // namespace silkworm::consensus
