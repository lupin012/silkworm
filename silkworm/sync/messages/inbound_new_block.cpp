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

#include "inbound_new_block.hpp"

#include <algorithm>

#include <silkworm/core/common/cast.hpp>
#include <silkworm/node/common/decoding_exception.hpp>
#include <silkworm/node/common/log.hpp>
#include <silkworm/sync/internals/body_sequence.hpp>
#include <silkworm/sync/internals/header_chain.hpp>
#include <silkworm/sync/internals/random_number.hpp>
#include <silkworm/sync/rpc/send_message_by_id.hpp>

namespace silkworm {

InboundNewBlock::InboundNewBlock(const sentry::InboundMessage& msg) {
    if (msg.id() != sentry::MessageId::NEW_BLOCK_66)
        throw std::logic_error("InboundNewBlock received wrong InboundMessage");

    reqId_ = RANDOM_NUMBER.generate_one();  // for trace purposes

    peerId_ = bytes_from_H512(msg.peer_id());

    ByteView data = string_view_to_byte_view(msg.data());  // copy for consumption
    success_or_throw(rlp::decode(data, packet_));

    SILK_TRACE << "Received message " << *this;
}

void InboundNewBlock::execute(db::ROAccess, HeaderChain&, BodySequence& bs, SentryClient&) {
    SILK_TRACE << "Processing message " << *this;

    // todo: complete implementation
    /*
    // use packet_.td ?
    hc.accept_header(packet_.block.header); // process as single header segment
    */
    bs.accept_new_block(packet_.block, peerId_);  // add to prefetched bodies
}

uint64_t InboundNewBlock::reqId() const { return reqId_; }

std::string InboundNewBlock::content() const {
    std::stringstream content;
    log::prepare_for_logging(content);
    content << packet_;
    return content.str();
}

}  // namespace silkworm
