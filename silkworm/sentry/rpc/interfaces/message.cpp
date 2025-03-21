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

#include "message.hpp"

#include <cassert>
#include <optional>

#include <silkworm/sentry/eth/message_id.hpp>
#include <silkworm/sentry/eth/status_message.hpp>

namespace silkworm::sentry::rpc::interfaces {

namespace proto = ::sentry;

static std::optional<eth::MessageId> eth_message_id(proto::MessageId proto_id) {
    switch (proto_id) {
        case proto::STATUS_66:
            return eth::MessageId::kStatus;
        case proto::NEW_BLOCK_HASHES_66:
            return eth::MessageId::kNewBlockHashes;
        case proto::NEW_BLOCK_66:
            return eth::MessageId::kNewBlock;
        case proto::TRANSACTIONS_66:
            return eth::MessageId::kTransactions;
        case proto::NEW_POOLED_TRANSACTION_HASHES_66:
            return eth::MessageId::kNewPooledTransactionHashes;
        case proto::GET_BLOCK_HEADERS_66:
            return eth::MessageId::kGetBlockHeaders;
        case proto::GET_BLOCK_BODIES_66:
            return eth::MessageId::kGetBlockBodies;
        case proto::GET_NODE_DATA_66:
            return eth::MessageId::kGetNodeData;
        case proto::GET_RECEIPTS_66:
            return eth::MessageId::kGetReceipts;
        case proto::GET_POOLED_TRANSACTIONS_66:
            return eth::MessageId::kGetPooledTransactions;
        case proto::BLOCK_HEADERS_66:
            return eth::MessageId::kBlockHeaders;
        case proto::BLOCK_BODIES_66:
            return eth::MessageId::kBlockBodies;
        case proto::NODE_DATA_66:
            return eth::MessageId::kNodeData;
        case proto::RECEIPTS_66:
            return eth::MessageId::kReceipts;
        case proto::POOLED_TRANSACTIONS_66:
            return eth::MessageId::kPooledTransactions;
        default:
            return std::nullopt;
    }
}

static proto::MessageId proto_message_id_from_eth_id(eth::MessageId eth_id) {
    switch (eth_id) {
        case eth::MessageId::kStatus:
            return proto::STATUS_66;
        case eth::MessageId::kNewBlockHashes:
            return proto::NEW_BLOCK_HASHES_66;
        case eth::MessageId::kNewBlock:
            return proto::NEW_BLOCK_66;
        case eth::MessageId::kTransactions:
            return proto::TRANSACTIONS_66;
        case eth::MessageId::kNewPooledTransactionHashes:
            return proto::NEW_POOLED_TRANSACTION_HASHES_66;
        case eth::MessageId::kGetBlockHeaders:
            return proto::GET_BLOCK_HEADERS_66;
        case eth::MessageId::kGetBlockBodies:
            return proto::GET_BLOCK_BODIES_66;
        case eth::MessageId::kGetNodeData:
            return proto::GET_NODE_DATA_66;
        case eth::MessageId::kGetReceipts:
            return proto::GET_RECEIPTS_66;
        case eth::MessageId::kGetPooledTransactions:
            return proto::GET_POOLED_TRANSACTIONS_66;
        case eth::MessageId::kBlockHeaders:
            return proto::BLOCK_HEADERS_66;
        case eth::MessageId::kBlockBodies:
            return proto::BLOCK_BODIES_66;
        case eth::MessageId::kNodeData:
            return proto::NODE_DATA_66;
        case eth::MessageId::kReceipts:
            return proto::RECEIPTS_66;
        case eth::MessageId::kPooledTransactions:
            return proto::POOLED_TRANSACTIONS_66;
        default:
            assert(false);
            return proto::STATUS_66;
    }
}

uint8_t message_id(proto::MessageId proto_id) {
    auto eth_id = eth_message_id(proto_id);
    assert(eth_id.has_value());
    if (!eth_id)
        return eth::StatusMessage::kId;

    return (static_cast<uint8_t>(eth_id.value()) + eth::StatusMessage::kId);
}

static proto::MessageId proto_message_id(uint8_t message_id) {
    assert(message_id >= eth::StatusMessage::kId);
    if (message_id < eth::StatusMessage::kId)
        return proto::STATUS_66;

    return proto_message_id_from_eth_id(static_cast<eth::MessageId>(message_id - eth::StatusMessage::kId));
}

static Bytes bytes_from_string(const std::string& s) {
    return Bytes{reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

sentry::common::Message message_from_outbound_data(const proto::OutboundMessageData& message_data) {
    return {
        message_id(message_data.id()),
        bytes_from_string(message_data.data()),
    };
}

proto::InboundMessage inbound_message_from_message(const sentry::common::Message& message) {
    proto::InboundMessage result;
    result.set_id(proto_message_id(message.id));
    result.set_data(message.data.data(), message.data.size());
    return result;
}

}  // namespace silkworm::sentry::rpc::interfaces
