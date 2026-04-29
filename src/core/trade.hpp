#pragma once
#include "core/enums.hpp"
#include "core/order.hpp"

namespace lob {

struct Trade {
    OrderId       aggressor_id   {0};
    OrderId       passive_id     {0};
    Price         price          {0};
    Quantity      quantity       {0};
    Timestamp     timestamp      {0};
    AggressorSide aggressor_side {AggressorSide::None};
};

struct PartialFill {
    OrderId  order_id      {0};
    Quantity filled_qty    {0};
    Quantity remaining_qty {0};
    Timestamp timestamp    {0};
};

struct ExecutionReport {
    OrderId     order_id        {0};
    OrderStatus status          {OrderStatus::New};
    Quantity    cumulative_qty  {0};   // total filled so far
    Quantity    remaining_qty   {0};
    Price       avg_fill_price  {0};   // cumulative value / cumulative qty
    Timestamp   timestamp       {0};
};

struct Rejection {
    OrderId      order_id  {0};
    RejectReason reason    {RejectReason::None};
    Timestamp    timestamp {0};
};

} // namespace lob
