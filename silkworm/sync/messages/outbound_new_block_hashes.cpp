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

#include "outbound_new_block_hashes.hpp"

#include <silkworm/core/rlp/encode_vector.hpp>
#include <silkworm/node/common/log.hpp>
#include <silkworm/sync/internals/body_sequence.hpp>
#include <silkworm/sync/internals/header_chain.hpp>
#include <silkworm/sync/rpc/send_message_to_all.hpp>

namespace silkworm {

OutboundNewBlockHashes::OutboundNewBlockHashes(bool f) : is_first_sync_{f} {}

void OutboundNewBlockHashes::execute(db::ROAccess, HeaderChain& hc, BodySequence&, SentryClient& sentry) {
    using namespace std::literals::chrono_literals;

    auto& announces_to_do = hc.announces_to_do();

    if (is_first_sync_) {
        announces_to_do.clear();  // We don't want to send announces to peers during first sync
        return;
    }

    if (announces_to_do.empty()) {
        SILK_TRACE << "No OutboundNewBlockHashes (announcements) message to send";
        return;
    }

    for (auto& announce : announces_to_do) {
        // packet_.emplace_back(announce.hash, announce.number); // requires c++20
        packet_.push_back({announce.hash, announce.number});
    }

    auto request = std::make_unique<sentry::OutboundMessageData>();  // create request

    request->set_id(sentry::MessageId::NEW_BLOCK_HASHES_66);

    Bytes rlp_encoding;
    rlp::encode(rlp_encoding, packet_);
    request->set_data(rlp_encoding.data(), rlp_encoding.length());  // copy

    SILK_TRACE << "Sending message OutboundNewBlockHashes (announcements) with send_message_to_all, content:"
               << packet_;

    rpc::SendMessageToAll rpc{std::move(request)};

    seconds_t timeout = 1s;
    rpc.timeout(timeout);
    rpc.do_not_throw_on_failure();

    sentry.exec_remotely(rpc);

    if (!rpc.status().ok()) {
        SILK_TRACE << "Failure of rpc OutboundNewBlockHashes " << packet_ << ": " << rpc.status().error_message();
        return;
    }

    sentry::SentPeers peers = rpc.reply();
    SILK_TRACE << "Received rpc result of OutboundNewBlockHashes: "
               << std::to_string(peers.peers_size()) + " peer(s)";

    announces_to_do.clear();  // clear announces from the queue
}

std::string OutboundNewBlockHashes::content() const {
    if (packet_.empty()) return "- no block hash announcements to do, not sent -";
    std::stringstream content;
    log::prepare_for_logging(content);
    content << packet_;
    return content.str();
}

}  // namespace silkworm
