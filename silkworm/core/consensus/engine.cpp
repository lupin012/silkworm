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

#include <utility>

#include <silkpre/secp256k1n.hpp>

#include <silkworm/core/chain/intrinsic_gas.hpp>
#include <silkworm/core/chain/protocol_param.hpp>
#include <silkworm/core/consensus/clique/engine.hpp>
#include <silkworm/core/consensus/ethash/engine.hpp>
#include <silkworm/core/consensus/merge/engine.hpp>
#include <silkworm/core/consensus/noproof/engine.hpp>

namespace silkworm::consensus {

ValidationResult pre_validate_transaction(const Transaction& txn, const evmc_revision rev, const uint64_t chain_id,
                                          const std::optional<intx::uint256>& base_fee_per_gas) {
    if (txn.chain_id.has_value()) {
        if (rev < EVMC_SPURIOUS_DRAGON || txn.chain_id.value() != chain_id) {
            return ValidationResult::kWrongChainId;
        }
    }

    if (txn.type == Transaction::Type::kEip2930) {
        if (rev < EVMC_BERLIN) {
            return ValidationResult::kUnsupportedTransactionType;
        }
    } else if (txn.type == Transaction::Type::kEip1559) {
        if (rev < EVMC_LONDON) {
            return ValidationResult::kUnsupportedTransactionType;
        }
    } else if (txn.type != Transaction::Type::kLegacy) {
        return ValidationResult::kUnsupportedTransactionType;
    }

    if (base_fee_per_gas.has_value() && txn.max_fee_per_gas < base_fee_per_gas.value()) {
        return ValidationResult::kMaxFeeLessThanBase;
    }

    // https://github.com/ethereum/EIPs/pull/3594
    if (txn.max_priority_fee_per_gas > txn.max_fee_per_gas) {
        return ValidationResult::kMaxPriorityFeeGreaterThanMax;
    }

    /* Should the sender already be present it means the validation of signature already occurred */
    if (!txn.from.has_value()) {
        if (!silkpre::is_valid_signature(txn.r, txn.s, rev >= EVMC_HOMESTEAD)) {
            return ValidationResult::kInvalidSignature;
        }
    }

    const intx::uint128 g0{intrinsic_gas(txn, rev)};
    if (txn.gas_limit < g0) {
        return ValidationResult::kIntrinsicGas;
    }

    // EIP-2681: Limit account nonce to 2^64-1
    if (txn.nonce >= UINT64_MAX) {
        return ValidationResult::kNonceTooHigh;
    }

    // EIP-3860: Limit and meter initcode
    const bool contract_creation{!txn.to};
    if (rev >= EVMC_SHANGHAI && contract_creation && txn.data.size() > param::kMaxInitCodeSize) {
        return ValidationResult::kMaxInitCodeSizeExceeded;
    }

    return ValidationResult::kOk;
}

static std::unique_ptr<IEngine> pre_merge_engine(const ChainConfig& chain_config) {
    switch (chain_config.seal_engine) {
        case SealEngineType::kEthash:
            return std::make_unique<EthashEngine>(chain_config);
        case SealEngineType::kNoProof:
            return std::make_unique<NoProofEngine>(chain_config);
        case SealEngineType::kClique:
            return std::make_unique<CliqueEngine>(chain_config);
        default:
            return nullptr;
    }
}

std::unique_ptr<IEngine> engine_factory(const ChainConfig& chain_config) {
    std::unique_ptr<IEngine> engine{pre_merge_engine(chain_config)};
    if (!engine) return nullptr;

    if (chain_config.terminal_total_difficulty.has_value()) {
        engine = std::make_unique<MergeEngine>(std::move(engine), chain_config);
    }

    return engine;
}

}  // namespace silkworm::consensus
