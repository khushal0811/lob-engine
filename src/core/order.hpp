#pragma once
#include "core/enums.hpp"
#include <cstdint>
#include <string>

namespace lob {

using OrderId   = uint64_t;
using Price     = int64_t;   // integer price in ticks (avoids float precision issues)
using Quantity  = uint64_t;
using Timestamp = uint64_t;  // nanoseconds since epoch

struct Order {
    OrderId     id          {0};
    Side        side        {Side::Buy};
    OrderType   type        {OrderType::Limit};
    Price       price       {0};       // limit price (0 for market orders)
    Price       stop_price  {0};       // trigger price for stop and stop-limit orders
    Quantity    quantity    {0};       // remaining quantity
    Quantity    orig_qty    {0};       // original quantity (set once, never changed)
    Quantity    peak_qty    {0};       // visible peak for iceberg (0 if not iceberg)
    Quantity    reserve_qty {0};       // hidden reserve for iceberg
    Timestamp   timestamp   {0};       // submission timestamp, used for time priority
    OrderStatus status      {OrderStatus::New};

    // Convenience helpers
    [[nodiscard]] bool is_buy()    const noexcept { return side == Side::Buy; }
    [[nodiscard]] bool is_sell()   const noexcept { return side == Side::Sell; }
    [[nodiscard]] bool is_resting() const noexcept {
        return status == OrderStatus::Resting || status == OrderStatus::PartialFill;
    }
    [[nodiscard]] bool is_iceberg() const noexcept { return peak_qty > 0; }
    [[nodiscard]] Quantity visible_qty() const noexcept {
        return is_iceberg() ? peak_qty : quantity;
    }
};

} // namespace lob
