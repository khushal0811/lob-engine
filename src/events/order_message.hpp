#pragma once
#include "core/enums.hpp"
#include <cstdint>
#include <string>

namespace lob::events {

// ---------------------------------------------------------------------------
// OrderAction — the two operations external clients may request.
// ---------------------------------------------------------------------------
enum class OrderAction : uint8_t { NewOrder, CancelOrder };

// ---------------------------------------------------------------------------
// OrderMessage — canonical inbound message format.
//
// All external systems MUST use this format. The gateway deserializes incoming
// JSON into this struct and forwards it to the ExchangeManager. No raw
// engine types are exposed to the network layer.
//
// Supported instruments: STOCK_1 … STOCK_25
// Supported order types: Limit, Market, Stop, StopLimit, Iceberg
// ---------------------------------------------------------------------------
struct OrderMessage {
    uint64_t    order_id{0};
    uint64_t    client_id{0};
    std::string symbol;          // "STOCK_1" … "STOCK_25"
    Side        side{Side::Buy};
    OrderType   type{OrderType::Limit};
    uint64_t    quantity{0};
    int64_t     price{0};        // limit price in ticks; 0 for market orders
    int64_t     stop_price{0};   // trigger price for Stop/StopLimit
    uint64_t    peak_qty{0};     // visible peak for Iceberg orders
    uint64_t    timestamp{0};    // client-supplied nanosecond timestamp
    OrderAction action{OrderAction::NewOrder};
};

} // namespace lob::events
