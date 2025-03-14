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

#include <silkworm/core/consensus/base/engine.hpp>

namespace silkworm::consensus {

// Consensus engine applicable to Proof-of-Stake blocks.
// See EIP-3675: Upgrade consensus to Proof-of-Stake.
class ProofOfStakeEngine : public EngineBase {
  public:
    explicit ProofOfStakeEngine(const ChainConfig& chain_config) : EngineBase(chain_config, /*prohibit_ommers=*/true) {}

    ValidationResult validate_seal(const BlockHeader& header) override;

    ValidationResult validate_difficulty(const BlockHeader& header, const BlockHeader& parent) override;

    void finalize(IntraBlockState& state, const Block& block, evmc_revision revision) override;
};

}  // namespace silkworm::consensus
