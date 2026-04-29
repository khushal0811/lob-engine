#include "engine/matching_engine.hpp"
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace lob {

MatchingEngine::MatchingEngine(EngineConfig config) : config_(std::move(config)) {}

MatchResult MatchingEngine::submit(const OrderEvent& event) {
    ++sequence_;
    return std::visit(
        [this](const auto& e) -> MatchResult {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, NewOrderEvent>)
                return process_new_order(e);
            else if constexpr (std::is_same_v<T, CancelOrderEvent>)
                return process_cancel(e);
            else if constexpr (std::is_same_v<T, ModifyOrderEvent>)
                return process_modify(e);
            else if constexpr (std::is_same_v<T, ReplaceOrderEvent>)
                return process_replace(e);
        },
        event);
}

MatchResult MatchingEngine::process_new_order(const NewOrderEvent& e) {
    MatchResult result;
    Order order = e.order;
    order.status = OrderStatus::Resting;

    if (order.type == OrderType::Limit) {
        match_limit(order, result);
        if (order.quantity > 0) {
            book_.insert_order(order);
        }
    } else if (order.type == OrderType::Market) {
        match_market(order, result);
    } else if (order.type == OrderType::Stop || order.type == OrderType::StopLimit) {
        bool immediately_triggered = false;
        if (order.is_buy() && book_.last_trade_price != 0 &&
            book_.last_trade_price >= order.stop_price)
            immediately_triggered = true;
        if (order.is_sell() && book_.last_trade_price != 0 &&
            book_.last_trade_price <= order.stop_price)
            immediately_triggered = true;

        if (immediately_triggered) {
            process_triggered_stop(order, result);
        } else {
            pending_stops_.push_back(order);
        }
    } else if (order.type == OrderType::Iceberg) {
        order.reserve_qty = order.quantity - order.peak_qty;
        order.quantity = order.peak_qty;
        match_limit(order, result);
        if (order.quantity > 0 || order.reserve_qty > 0) {
            book_.insert_order(order);
        }
    } else {
        assert(false && "Unsupported order type");
    }

    return result;
}

// ---------------------------------------------------------------------------
// Limit order matching — price-time priority against the opposite side.
// ---------------------------------------------------------------------------
void MatchingEngine::match_limit(Order& aggressor, MatchResult& result) {
    while (aggressor.quantity > 0) {
        PriceLevel* passive_level =
            aggressor.is_buy() ? book_.best_ask_level() : book_.best_bid_level();

        if (!passive_level)
            break;

        if (aggressor.is_buy() && passive_level->price() > aggressor.price)
            break;
        if (aggressor.is_sell() && passive_level->price() < aggressor.price)
            break;

        OrderId passive_id = passive_level->front();
        Order* passive = book_.find_order_mut(passive_id);
        assert(passive != nullptr);

        Quantity fill_qty = std::min(aggressor.quantity, passive->quantity);
        execute_fill(aggressor, *passive, fill_qty, result);
    }

    if (aggressor.quantity == 0)
        aggressor.status = OrderStatus::Filled;
}

// ---------------------------------------------------------------------------
// Market order matching — consumes liquidity at any available price.
// ---------------------------------------------------------------------------
void MatchingEngine::match_market(Order& aggressor, MatchResult& result) {
    while (aggressor.quantity > 0) {
        PriceLevel* passive_level =
            aggressor.is_buy() ? book_.best_ask_level() : book_.best_bid_level();

        if (!passive_level) {
            Rejection rej;
            rej.order_id = aggressor.id;
            rej.reason = RejectReason::MarketExhausted;
            result.rejections.push_back(rej);
            aggressor.status = OrderStatus::Cancelled;
            break;
        }

        OrderId passive_id = passive_level->front();
        Order* passive = book_.find_order_mut(passive_id);
        Quantity fill_qty = std::min(aggressor.quantity, passive->quantity);

        execute_fill(aggressor, *passive, fill_qty, result);
    }

    if (aggressor.quantity == 0)
        aggressor.status = OrderStatus::Filled;
}

// ---------------------------------------------------------------------------
// Fill execution — shared path for limit and market matching.
// ---------------------------------------------------------------------------
void MatchingEngine::execute_fill(Order& aggressor, Order& passive, Quantity fill_qty,
                                  MatchResult& result) {
    Price fill_price = passive.price;

    aggressor.quantity -= fill_qty;
    passive.quantity -= fill_qty;

    book_.last_trade_price = fill_price;

    Trade trade;
    trade.aggressor_id = aggressor.id;
    trade.passive_id = passive.id;
    trade.price = fill_price;
    trade.quantity = fill_qty;
    trade.aggressor_side = aggressor.is_buy() ? AggressorSide::Buy : AggressorSide::Sell;
    result.trades.push_back(trade);

    OrderId passive_id = passive.id;

    if (passive.quantity == 0 && passive.is_iceberg() && passive.reserve_qty > 0) {
        replenish_iceberg(passive, fill_qty, result);
    } else if (passive.quantity == 0) {
        passive.status = OrderStatus::Filled;
        book_.cancel_order(passive_id);
    } else {
        PriceLevel* lvl = book_.get_level(passive.side, passive.price);
        if (lvl)
            lvl->reduce_front_volume(fill_qty);
        book_.update_order_quantity(passive_id, passive.quantity);
    }

    evaluate_stop_triggers(result);
}

// ---------------------------------------------------------------------------
// Iceberg replenishment — restores the visible peak from the hidden reserve.
// The order moves to the back of the price level, resetting time priority.
// ---------------------------------------------------------------------------
void MatchingEngine::replenish_iceberg(Order& passive, Quantity filled_qty, MatchResult& result) {
    (void)result;
    PriceLevel* level = book_.get_level(passive.side, passive.price);
    if (!level)
        return;

    level->pop_front(filled_qty);

    Quantity new_peak = std::min(passive.peak_qty, passive.reserve_qty);
    passive.reserve_qty -= new_peak;
    passive.quantity = new_peak;

    level->push_back(passive.id, new_peak);
    book_.update_order_quantity(passive.id, new_peak);
}

// ---------------------------------------------------------------------------
// Stop order trigger evaluation.
//
// pending_stops_ is swapped into a local vector before iteration so that
// any fills generated by a triggered stop cannot re-enter this function
// and re-evaluate the same pending list.
// ---------------------------------------------------------------------------
void MatchingEngine::evaluate_stop_triggers(MatchResult& result) {
    if (pending_stops_.empty())
        return;

    Price last_price = book_.last_trade_price;
    if (last_price == 0)
        return;

    std::vector<Order> working;
    std::swap(working, pending_stops_);

    for (auto& stop : working) {
        bool triggered = false;
        if (stop.is_buy() && last_price >= stop.stop_price)
            triggered = true;
        if (stop.is_sell() && last_price <= stop.stop_price)
            triggered = true;

        if (triggered) {
            process_triggered_stop(stop, result);
        } else {
            pending_stops_.push_back(stop);
        }
    }
}

void MatchingEngine::process_triggered_stop(Order& stop, MatchResult& result) {
    stop.status = OrderStatus::Triggered;

    Order converted = stop;
    if (stop.type == OrderType::Stop) {
        converted.type = OrderType::Market;
        converted.price = 0;
    } else {
        converted.type = OrderType::Limit;
    }

    auto sub_result = process_new_order(NewOrderEvent{converted});
    result.trades.insert(result.trades.end(), sub_result.trades.begin(), sub_result.trades.end());
    result.reports.insert(result.reports.end(), sub_result.reports.begin(),
                          sub_result.reports.end());
    result.rejections.insert(result.rejections.end(), sub_result.rejections.begin(),
                             sub_result.rejections.end());
}

// ---------------------------------------------------------------------------
// Cancel / Modify / Replace
// ---------------------------------------------------------------------------
MatchResult MatchingEngine::process_cancel(const CancelOrderEvent& e) {
    MatchResult result;

    for (auto it = pending_stops_.begin(); it != pending_stops_.end(); ++it) {
        if (it->id == e.order_id) {
            ExecutionReport rep;
            rep.order_id = e.order_id;
            rep.status = OrderStatus::Cancelled;
            rep.remaining_qty = it->quantity;
            rep.timestamp = e.timestamp;
            result.reports.push_back(rep);
            pending_stops_.erase(it);
            return result;
        }
    }

    auto order_opt = book_.find_order(e.order_id);
    if (!order_opt) {
        Rejection rej;
        rej.order_id = e.order_id;
        rej.reason = RejectReason::OrderNotFound;
        rej.timestamp = e.timestamp;
        result.rejections.push_back(rej);
        return result;
    }

    if (!order_opt->is_resting()) {
        Rejection rej;
        rej.order_id = e.order_id;
        rej.reason = RejectReason::OrderNotCancellable;
        rej.timestamp = e.timestamp;
        result.rejections.push_back(rej);
        return result;
    }

    book_.cancel_order(e.order_id);

    ExecutionReport rep;
    rep.order_id = e.order_id;
    rep.status = OrderStatus::Cancelled;
    rep.remaining_qty = order_opt->quantity;
    rep.timestamp = e.timestamp;
    result.reports.push_back(rep);

    return result;
}

MatchResult MatchingEngine::process_modify(const ModifyOrderEvent& e) {
    MatchResult result;

    auto order_opt = book_.find_order(e.order_id);
    if (!order_opt || !order_opt->is_resting()) {
        Rejection rej;
        rej.order_id = e.order_id;
        rej.reason = !order_opt ? RejectReason::OrderNotFound : RejectReason::OrderNotCancellable;
        result.rejections.push_back(rej);
        return result;
    }

    Order order = *order_opt;
    bool price_changed = (e.new_price != 0 && e.new_price != order.price);
    bool qty_increased = (e.new_quantity != 0 && e.new_quantity > order.quantity);

    if (price_changed || qty_increased) {
        // Price change or quantity increase: cancel and reinsert to reset time priority.
        book_.cancel_order(e.order_id);
        if (e.new_price != 0)
            order.price = e.new_price;
        if (e.new_quantity != 0)
            order.quantity = e.new_quantity;
        order.timestamp = e.timestamp;
        book_.insert_order(order);
    } else {
        if (e.new_quantity != 0 && e.new_quantity < order.quantity) {
            // Quantity reduction preserves time priority.
            Quantity delta = order.quantity - e.new_quantity;
            book_.update_order_quantity(e.order_id, e.new_quantity);
            book_.reduce_level_volume(order.side, order.price, delta);
        }
    }

    return result;
}

MatchResult MatchingEngine::process_replace(const ReplaceOrderEvent& e) {
    MatchResult result;

    if (!book_.has_order(e.old_order_id)) {
        Rejection rej;
        rej.order_id = e.old_order_id;
        rej.reason = RejectReason::OrderNotFound;
        result.rejections.push_back(rej);
        return result;
    }

    book_.cancel_order(e.old_order_id);

    auto new_result = process_new_order(NewOrderEvent{e.new_order});
    result.trades.insert(result.trades.end(), new_result.trades.begin(), new_result.trades.end());
    result.reports.insert(result.reports.end(), new_result.reports.begin(),
                          new_result.reports.end());
    result.rejections.insert(result.rejections.end(), new_result.rejections.begin(),
                             new_result.rejections.end());
    return result;
}

} // namespace lob
