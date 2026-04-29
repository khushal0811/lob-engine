#pragma once
#include "core/enums.hpp"
#include "core/order.hpp"
#include <variant>

namespace lob {

struct NewOrderEvent {
    Order order;
};

struct CancelOrderEvent {
    OrderId order_id{0};
    Timestamp timestamp{0};
};

struct ModifyOrderEvent {
    OrderId order_id{0};
    Price new_price{0};       // 0 = no change
    Quantity new_quantity{0}; // 0 = no change
    Timestamp timestamp{0};
};

struct ReplaceOrderEvent {
    OrderId old_order_id{0};
    Order new_order; // fully specified new order
};

using OrderEvent =
    std::variant<NewOrderEvent, CancelOrderEvent, ModifyOrderEvent, ReplaceOrderEvent>;

} // namespace lob
