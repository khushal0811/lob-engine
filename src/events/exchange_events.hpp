#pragma once
#include "core/enums.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace lob::events {

// ---------------------------------------------------------------------------
// All exchange output events.
//
// These are external wire-format types only. They are never used inside the
// matching engine, order book, or any internal component. Consumers interact
// exclusively through these structs.
// ---------------------------------------------------------------------------

struct OrderAcceptedEvent {
    uint64_t    timestamp{0};
    std::string symbol;
    uint64_t    order_id{0};
    uint64_t    client_id{0};
    int64_t     price{0};
    uint64_t    quantity{0};
    Side        side{Side::Buy};
    OrderType   type{OrderType::Limit};
};

struct OrderRejectedEvent {
    uint64_t     timestamp{0};
    std::string  symbol;
    uint64_t     order_id{0};
    uint64_t     client_id{0};
    RejectReason reason{RejectReason::None};
};

struct OrderCanceledEvent {
    uint64_t    timestamp{0};
    std::string symbol;
    uint64_t    order_id{0};
    uint64_t    remaining_qty{0};
};

struct TradeExecutedEvent {
    uint64_t    timestamp{0};
    std::string symbol;
    uint64_t    buy_order_id{0};
    uint64_t    sell_order_id{0};
    int64_t     price{0};
    uint64_t    quantity{0};
};

struct BookUpdatedEvent {
    uint64_t    timestamp{0};
    std::string symbol;
    int64_t     best_bid{0};  // 0 = no bid
    int64_t     best_ask{0};  // 0 = no ask
    int64_t     spread{0};
    std::size_t bid_levels{0};
    std::size_t ask_levels{0};
};

struct SnapshotEvent {
    uint64_t    timestamp{0};
    std::string symbol;
    int64_t     last_trade_price{0};
    int64_t     best_bid{0};
    int64_t     best_ask{0};
    // Top-of-book depth: (price_ticks, total_quantity)
    std::vector<std::pair<int64_t, uint64_t>> bids;
    std::vector<std::pair<int64_t, uint64_t>> asks;
};

// Unified variant used throughout the gateway and serialization layers.
using ExchangeEvent = std::variant<OrderAcceptedEvent, OrderRejectedEvent, OrderCanceledEvent,
                                   TradeExecutedEvent, BookUpdatedEvent, SnapshotEvent>;

} // namespace lob::events
