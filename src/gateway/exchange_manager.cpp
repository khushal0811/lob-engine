#include "gateway/exchange_manager.hpp"
#include <cassert>
#include <time.h>

namespace lob::gateway {

// ---------------------------------------------------------------------------
// Construction — create 25 instruments, build symbol index
// ---------------------------------------------------------------------------

ExchangeManager::ExchangeManager(EngineConfig cfg) {
    instruments_.reserve(kInstrumentCount);
    for (std::size_t i = 1; i <= kInstrumentCount; ++i) {
        std::string sym = "STOCK_" + std::to_string(i);
        instruments_.emplace_back(sym, cfg);
        index_[sym] = i - 1;
    }
}

bool ExchangeManager::has_symbol(const std::string& sym) const noexcept {
    return index_.count(sym) > 0;
}

// ---------------------------------------------------------------------------
// Timestamp helper
// ---------------------------------------------------------------------------

uint64_t ExchangeManager::now_ns() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// process — route order to the correct engine, translate result
// ---------------------------------------------------------------------------

std::vector<events::ExchangeEvent> ExchangeManager::process(const events::OrderMessage& msg) {
    auto it = index_.find(msg.symbol);
    if (it == index_.end())
        return {}; // unknown symbol — gateway should have rejected this

    Instrument& instr = instruments_[it->second];

    if (msg.action == events::OrderAction::CancelOrder) {
        CancelOrderEvent ev;
        ev.order_id  = msg.order_id;
        ev.timestamp = msg.timestamp;
        auto result  = instr.engine.submit(ev);
        return translate_result(msg, result, instr.engine.book());
    }

    Order order;
    order.id         = msg.order_id;
    order.side       = msg.side;
    order.type       = msg.type;
    order.price      = msg.price;
    order.stop_price = msg.stop_price;
    order.quantity   = msg.quantity;
    order.orig_qty   = msg.quantity;
    order.peak_qty   = msg.peak_qty;
    order.timestamp  = (msg.timestamp != 0) ? msg.timestamp : now_ns();
    order.status     = OrderStatus::New;

    auto result = instr.engine.submit(NewOrderEvent{order});
    return translate_result(msg, result, instr.engine.book());
}

// ---------------------------------------------------------------------------
// translate_result — MatchResult → vector<ExchangeEvent>
//
// Translation rules:
//   Each Trade           → TradeExecutedEvent
//   Each Rejection       → OrderRejectedEvent
//   ExecutionReport/Cancelled → OrderCanceledEvent
//   New order, no reject → OrderAcceptedEvent
//   Any state change     → BookUpdatedEvent (appended last)
// ---------------------------------------------------------------------------

std::vector<events::ExchangeEvent>
ExchangeManager::translate_result(const events::OrderMessage& msg,
                                  const MatchResult&          res,
                                  const OrderBook&            book) const {
    std::vector<events::ExchangeEvent> out;
    out.reserve(res.trades.size() + res.rejections.size() + res.reports.size() + 2);

    const uint64_t ts = now_ns();

    for (const auto& rej : res.rejections) {
        events::OrderRejectedEvent ev;
        ev.timestamp = ts;
        ev.symbol    = msg.symbol;
        ev.order_id  = rej.order_id;
        ev.client_id = msg.client_id;
        ev.reason    = rej.reason;
        out.push_back(std::move(ev));
    }

    // Cancel confirmations (ExecutionReport with Cancelled status)
    for (const auto& rep : res.reports) {
        if (rep.status == OrderStatus::Cancelled) {
            events::OrderCanceledEvent ev;
            ev.timestamp     = ts;
            ev.symbol        = msg.symbol;
            ev.order_id      = rep.order_id;
            ev.remaining_qty = rep.remaining_qty;
            out.push_back(std::move(ev));
        }
    }

    for (const auto& trade : res.trades) {
        events::TradeExecutedEvent ev;
        ev.timestamp    = ts;
        ev.symbol       = msg.symbol;
        ev.buy_order_id  = (trade.aggressor_side == AggressorSide::Buy)
                               ? trade.aggressor_id
                               : trade.passive_id;
        ev.sell_order_id = (trade.aggressor_side == AggressorSide::Buy)
                               ? trade.passive_id
                               : trade.aggressor_id;
        ev.price    = trade.price;
        ev.quantity = trade.quantity;
        out.push_back(std::move(ev));
    }

    // For new orders with no rejections: emit OrderAccepted
    if (msg.action == events::OrderAction::NewOrder && res.rejections.empty()) {
        events::OrderAcceptedEvent ev;
        ev.timestamp = ts;
        ev.symbol    = msg.symbol;
        ev.order_id  = msg.order_id;
        ev.client_id = msg.client_id;
        ev.price     = msg.price;
        ev.quantity  = msg.quantity;
        ev.side      = msg.side;
        ev.type      = msg.type;
        out.push_back(std::move(ev));
    }

    // Book update — always emitted after any order action
    {
        events::BookUpdatedEvent ev;
        ev.timestamp  = ts;
        ev.symbol     = msg.symbol;
        ev.best_bid   = book.best_bid().value_or(0);
        ev.best_ask   = book.best_ask().value_or(0);
        ev.spread     = book.spread().value_or(0);
        ev.bid_levels = book.bid_level_count();
        ev.ask_levels = book.ask_level_count();
        out.push_back(std::move(ev));
    }

    return out;
}

// ---------------------------------------------------------------------------
// Snapshot generation (called from exchange thread only)
// ---------------------------------------------------------------------------

events::SnapshotEvent ExchangeManager::snapshot(const std::string& symbol) const {
    auto it = index_.find(symbol);
    assert(it != index_.end());

    const Instrument& instr = instruments_[it->second];
    const OrderBook&  book  = instr.engine.book();

    events::SnapshotEvent snap;
    snap.timestamp        = now_ns();
    snap.symbol           = symbol;
    snap.last_trade_price = book.last_trade_price;
    snap.best_bid         = book.best_bid().value_or(0);
    snap.best_ask         = book.best_ask().value_or(0);

    snap.bids.reserve(book.bid_level_count());
    for (const auto& [price, level] : book.bids())
        snap.bids.emplace_back(price, level.total_volume());

    snap.asks.reserve(book.ask_level_count());
    for (const auto& [price, level] : book.asks())
        snap.asks.emplace_back(price, level.total_volume());

    return snap;
}

std::vector<events::SnapshotEvent> ExchangeManager::snapshot_all() const {
    std::vector<events::SnapshotEvent> snaps;
    snaps.reserve(kInstrumentCount);
    for (const auto& instr : instruments_)
        snaps.push_back(snapshot(instr.symbol));
    return snaps;
}

} // namespace lob::gateway
