#pragma once
#include "book/price_level.hpp"
#include "core/order.hpp"
#include "core/trade.hpp"
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>

namespace lob {

class OrderBook {
public:
    // Insertion
    void insert_order(const Order& order);

    // Removal
    bool cancel_order(OrderId id); // returns false if not found

    // Lookup
    [[nodiscard]] std::optional<Order> find_order(OrderId id) const;
    [[nodiscard]] bool has_order(OrderId id) const noexcept;

    // Best prices
    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;
    [[nodiscard]] std::optional<Price> mid_price() const noexcept;
    [[nodiscard]] std::optional<Price> spread() const noexcept;

    // Level access (for matching engine)
    PriceLevel* best_bid_level();
    PriceLevel* best_ask_level();
    Order* find_order_mut(OrderId id);

    // Mutation helpers exposed for matching engine
    void remove_best_ask_level();
    void remove_best_bid_level();
    void update_order_quantity(OrderId id, Quantity new_qty);
    void reduce_level_volume(Side side, Price price, Quantity delta);
    PriceLevel* get_level(Side side, Price price);

    // Introspection
    [[nodiscard]] std::size_t order_count() const noexcept { return id_map_.size(); }
    [[nodiscard]] std::size_t bid_level_count() const noexcept { return bids_.size(); }
    [[nodiscard]] std::size_t ask_level_count() const noexcept { return asks_.size(); }

    Price last_trade_price{0};

    // Iteration for snapshot and metrics (returns sorted levels)
    const std::map<Price, PriceLevel, std::greater<Price>>& bids() const { return bids_; }
    const std::map<Price, PriceLevel>& asks() const { return asks_; }

private:
    // Bids: descending (highest price first — std::greater)
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    // Asks: ascending (lowest price first — default)
    std::map<Price, PriceLevel> asks_;
    // O(1) order lookup by ID
    std::unordered_map<OrderId, Order> id_map_;
};

} // namespace lob
