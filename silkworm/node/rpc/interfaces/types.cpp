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

#include "types.hpp"

#include <cstring>

#include <evmc/evmc.hpp>

#include <silkworm/core/common/endian.hpp>

namespace silkworm {

std::unique_ptr<types::H512> to_H512(const Bytes& orig) {
    using types::H128, types::H256, types::H512, evmc::load64be;

    Bytes bytes(64, 0);
    uint8_t* data = bytes.data();
    std::memcpy(data, orig.data(), orig.length() < 64 ? orig.length() : 64);

    H128* hi_hi = new H128{};
    H128* hi_lo = new H128{};
    H128* lo_hi = new H128{};
    H128* lo_lo = new H128{};

    hi_hi->set_hi(load64be(data + 0));
    hi_hi->set_lo(load64be(data + 8));
    hi_lo->set_hi(load64be(data + 16));
    hi_lo->set_lo(load64be(data + 24));
    lo_hi->set_hi(load64be(data + 32));
    lo_hi->set_lo(load64be(data + 40));
    lo_lo->set_hi(load64be(data + 48));
    lo_lo->set_lo(load64be(data + 56));

    H256* hi = new H256{};
    hi->set_allocated_hi(hi_hi);
    hi->set_allocated_lo(hi_lo);
    H256* lo = new H256{};
    lo->set_allocated_hi(lo_hi);
    lo->set_allocated_lo(lo_lo);

    auto dest = std::make_unique<H512>();
    dest->set_allocated_hi(hi);  // take ownership
    dest->set_allocated_lo(lo);  // take ownership

    return dest;  // transfer ownership
}

Bytes bytes_from_H512(const types::H512& orig) {
    uint64_t hi_hi_hi = orig.hi().hi().hi();
    uint64_t hi_hi_lo = orig.hi().hi().lo();
    uint64_t hi_lo_hi = orig.hi().lo().hi();
    uint64_t hi_lo_lo = orig.hi().lo().lo();
    uint64_t lo_hi_hi = orig.lo().hi().hi();
    uint64_t lo_hi_lo = orig.lo().hi().lo();
    uint64_t lo_lo_hi = orig.lo().lo().hi();
    uint64_t lo_lo_lo = orig.lo().lo().lo();

    Bytes dest(64, 0);
    auto data = dest.data();
    endian::store_big_u64(data + 0, hi_hi_hi);
    endian::store_big_u64(data + 8, hi_hi_lo);
    endian::store_big_u64(data + 16, hi_lo_hi);
    endian::store_big_u64(data + 24, hi_lo_lo);
    endian::store_big_u64(data + 32, lo_hi_hi);
    endian::store_big_u64(data + 40, lo_hi_lo);
    endian::store_big_u64(data + 48, lo_lo_hi);
    endian::store_big_u64(data + 56, lo_lo_lo);

    return dest;
}

Hash hash_from_H256(const types::H256& orig) {
    uint64_t hi_hi = orig.hi().hi();
    uint64_t hi_lo = orig.hi().lo();
    uint64_t lo_hi = orig.lo().hi();
    uint64_t lo_lo = orig.lo().lo();

    Hash dest;
    endian::store_big_u64(dest.bytes + 0, hi_hi);
    endian::store_big_u64(dest.bytes + 8, hi_lo);
    endian::store_big_u64(dest.bytes + 16, lo_hi);
    endian::store_big_u64(dest.bytes + 24, lo_lo);

    return dest;
}

constexpr uint64_t& lo_lo(intx::uint256& x) { return x[0]; }
constexpr uint64_t& lo_hi(intx::uint256& x) { return x[1]; }
constexpr uint64_t& hi_lo(intx::uint256& x) { return x[2]; }
constexpr uint64_t& hi_hi(intx::uint256& x) { return x[3]; }

intx::uint256 uint256_from_H256(const types::H256& orig) {
    using types::H128, types::H256;

    intx::uint256 dest;
    hi_hi(dest) = orig.hi().hi();
    hi_lo(dest) = orig.hi().lo();
    lo_hi(dest) = orig.lo().hi();
    lo_lo(dest) = orig.lo().lo();

    return dest;
}

}  // namespace silkworm
