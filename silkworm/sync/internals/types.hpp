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

#include <chrono>

#include <magic_enum.hpp>

#include <silkworm/core/common/assert.hpp>
#include <silkworm/core/common/cast.hpp>
#include <silkworm/core/common/util.hpp>
#include <silkworm/core/rlp/decode.hpp>
#include <silkworm/core/rlp/encode.hpp>
#include <silkworm/core/types/block.hpp>
#include <silkworm/core/types/hash.hpp>
#include <silkworm/core/types/transaction.hpp>

namespace silkworm {

using BigInt = intx::uint256;  // use intx::to_string, from_string, ...

// using Bytes = std::basic_string<uint8_t>; already defined elsewhere
// using std::string to_hex(ByteView bytes);
// using std::optional<Bytes> from_hex(std::string_view hex) noexcept;

using time_point_t = std::chrono::time_point<std::chrono::system_clock>;
using duration_t = std::chrono::system_clock::duration;
using seconds_t = std::chrono::seconds;
using milliseconds_t = std::chrono::milliseconds;

// stream operator <<
inline std::ostream& operator<<(std::ostream& out, const silkworm::ByteView& bytes) {
    out << silkworm::to_hex(bytes);
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const evmc::address& addr) {
    out << silkworm::to_hex(addr);
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const evmc::bytes32& b32) {
    out << silkworm::to_hex(b32);
    return out;
}

// Peers
using PeerId = Bytes;

static inline const PeerId no_peer{byte_ptr_cast("")};

// Bytes already has operator<<, so PeerId but PeerId is too long
inline Bytes human_readable_id(const PeerId& peer_id) {
    return {peer_id.data(), std::min<size_t>(peer_id.length(), 20)};
}

enum Penalty : int {
    NoPenalty = 0,
    BadBlockPenalty,
    DuplicateHeaderPenalty,
    WrongChildBlockHeightPenalty,
    WrongChildDifficultyPenalty,
    InvalidSealPenalty,
    TooFarFuturePenalty,
    TooFarPastPenalty,
    AbandonedAnchorPenalty
};

struct PeerPenalization {
    Penalty penalty;
    PeerId peerId;

    PeerPenalization(Penalty p, PeerId id) : penalty(p), peerId(std::move(id)) {}  // unnecessary with c++20
};

inline std::ostream& operator<<(std::ostream& os, const PeerPenalization& penalization) {
    os << "peerId=" << penalization.peerId << " cause=" << magic_enum::enum_name(penalization.penalty);
    return os;
}

struct Announce {
    Hash hash;
    BlockNum number = 0;
};

// RLP
namespace rlp {
    inline size_t length(const Hash&) { return kHashLength + 1; }

    void encode(Bytes& to, const Hash& h);

    template <>
    DecodingResult decode(ByteView& from, Hash& to) noexcept;

}  // namespace rlp

}  // namespace silkworm

namespace std {

template <>
struct hash<silkworm::Hash> : public std::hash<evmc::bytes32>  // to use Hash with std::unordered_set/map
{};

}  // namespace std
