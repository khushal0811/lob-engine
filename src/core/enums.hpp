#pragma once
#include <cstdint>

namespace lob {

enum class Side : uint8_t { Buy, Sell };

enum class OrderType : uint8_t { Limit, Market, Stop, StopLimit, Iceberg };

enum class OrderStatus : uint8_t {
    New,         // accepted, not yet on book
    Resting,     // on book, waiting for match
    PartialFill, // partially filled, remainder resting
    Filled,      // fully filled, removed from book
    Cancelled,   // cancelled by request or exhaustion
    Rejected,    // failed validation
    Triggered    // stop order that has been converted
};

enum class RejectReason : uint8_t {
    None,
    InvalidPrice,
    InvalidQuantity,
    DuplicateOrderId,
    OrderNotFound,
    OrderNotCancellable,
    InvalidModify,
    StopPriceInconsistent,
    PriceBandViolation,
    MaxSizeViolation,
    MarketExhausted
};

enum class EventType : uint8_t { NewOrder, CancelOrder, ModifyOrder, ReplaceOrder };

enum class AggressorSide : uint8_t { Buy, Sell, None };

} // namespace lob
