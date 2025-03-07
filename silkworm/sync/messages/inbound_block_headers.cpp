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

#include "inbound_block_headers.hpp"

#include <silkworm/core/common/cast.hpp>
#include <silkworm/node/common/decoding_exception.hpp>
#include <silkworm/node/common/log.hpp>
#include <silkworm/sync/internals/body_sequence.hpp>
#include <silkworm/sync/internals/header_chain.hpp>
#include <silkworm/sync/messages/outbound_get_block_headers.hpp>
#include <silkworm/sync/rpc/peer_min_block.hpp>
#include <silkworm/sync/rpc/penalize_peer.hpp>

namespace silkworm {

InboundBlockHeaders::InboundBlockHeaders(const sentry::InboundMessage& msg) {
    if (msg.id() != sentry::MessageId::BLOCK_HEADERS_66)
        throw std::logic_error("InboundBlockHeaders received wrong InboundMessage");

    peerId_ = bytes_from_H512(msg.peer_id());

    ByteView data = string_view_to_byte_view(msg.data());  // copy for consumption
    success_or_throw(rlp::decode(data, packet_));

    SILK_TRACE << "Received message " << *this;
}

void InboundBlockHeaders::execute(db::ROAccess, HeaderChain& hc, BodySequence&, SentryClient& sentry) {
    using namespace std;

    SILK_TRACE << "Processing message " << *this;

    BlockNum highestBlock = 0;
    for (BlockHeader& header : packet_.request) {
        highestBlock = std::max(highestBlock, header.number);
    }

    // Save the headers
    auto [penalty, requestMoreHeaders] = hc.accept_headers(packet_.request, packet_.requestId, peerId_);

    // Reply
    if (penalty != Penalty::NoPenalty) {
        SILK_TRACE << "Replying to " << identify(*this) << " with penalize_peer";
        SILK_TRACE << "Penalizing " << PeerPenalization(penalty, peerId_);
        rpc::PenalizePeer penalize_peer(peerId_, penalty);
        penalize_peer.do_not_throw_on_failure();
        sentry.exec_remotely(penalize_peer);
    }

    SILK_TRACE << "Replying to " << identify(*this) << " with peer_min_block";
    rpc::PeerMinBlock rpc(peerId_, highestBlock);
    rpc.do_not_throw_on_failure();
    sentry.exec_remotely(rpc);

    if (!rpc.status().ok()) {
        SILK_TRACE << "Failure of the replay to rpc " << identify(*this) << ": " << rpc.status().error_message();
    }
}

uint64_t InboundBlockHeaders::reqId() const { return packet_.requestId; }

std::string InboundBlockHeaders::content() const {
    std::stringstream content;
    log::prepare_for_logging(content);
    content << packet_;
    return content.str();
}

}  // namespace silkworm