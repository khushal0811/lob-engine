#include "book/order_book.hpp"

namespace lob {

void OrderBook::insert_order(const Order& order) {
    id_map_[order.id] = order;
    if (order.is_buy()) {
        auto [it, inserted] = bids_.emplace(order.price, PriceLevel{order.price});
        it->second.push_back(order.id, order.visible_qty());
    } else {
        auto [it, inserted] = asks_.emplace(order.price, PriceLevel{order.price});
        it->second.push_back(order.id, order.visible_qty());
    }
}

bool OrderBook::cancel_order(OrderId id) {
    auto it = id_map_.find(id);
    if (it == id_map_.end())
        return false;
    const Order& order = it->second;
    if (order.is_buy()) {
        auto lvl_it = bids_.find(order.price);
        if (lvl_it != bids_.end()) {
            lvl_it->second.remove(id, order.visible_qty());
            if (lvl_it->second.empty())
                bids_.erase(lvl_it);
        }
    } else {
        auto lvl_it = asks_.find(order.price);
        if (lvl_it != asks_.end()) {
            lvl_it->second.remove(id, order.visible_qty());
            if (lvl_it->second.empty())
                asks_.erase(lvl_it);
        }
    }
    id_map_.erase(it);
    return true;
}

std::optional<Order> OrderBook::find_order(OrderId id) const {
    auto it = id_map_.find(id);
    if (it == id_map_.end())
        return std::nullopt;
    return it->second;
}

bool OrderBook::has_order(OrderId id) const noexcept { return id_map_.count(id) > 0; }

Order* OrderBook::find_order_mut(OrderId id) {
    auto it = id_map_.find(id);
    if (it == id_map_.end())
        return nullptr;
    return &it->second;
}

std::optional<Price> OrderBook::best_bid() const noexcept {
    if (bids_.empty())
        return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    if (asks_.empty())
        return std::nullopt;
    return asks_.begin()->first;
}

std::optional<Price> OrderBook::mid_price() const noexcept {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba)
        return std::nullopt;
    return (*bb + *ba) / 2;
}

std::optional<Price> OrderBook::spread() const noexcept {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba)
        return std::nullopt;
    return *ba - *bb;
}

PriceLevel* OrderBook::best_bid_level() {
    if (bids_.empty())
        return nullptr;
    return &bids_.begin()->second;
}

PriceLevel* OrderBook::best_ask_level() {
    if (asks_.empty())
        return nullptr;
    return &asks_.begin()->second;
}

void OrderBook::remove_best_ask_level() {
    if (!asks_.empty())
        asks_.erase(asks_.begin());
}

void OrderBook::remove_best_bid_level() {
    if (!bids_.empty())
        bids_.erase(bids_.begin());
}

void OrderBook::update_order_quantity(OrderId id, Quantity new_qty) {
    auto it = id_map_.find(id);
    if (it != id_map_.end())
        it->second.quantity = new_qty;
}

void OrderBook::reduce_level_volume(Side side, Price price, Quantity delta) {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it != bids_.end())
            it->second.reduce_front_volume(delta);
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end())
            it->second.reduce_front_volume(delta);
    }
}

PriceLevel* OrderBook::get_level(Side side, Price price) {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it != bids_.end() ? &it->second : nullptr;
    } else {
        auto it = asks_.find(price);
        return it != asks_.end() ? &it->second : nullptr;
    }
}

} // namespace lob
