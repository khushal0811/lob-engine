#pragma once
#include "book/order_book.hpp"
#include "core/config.hpp"
#include "core/event.hpp"
#include "core/trade.hpp"
#include <functional>
#include <vector>

namespace lob {

struct MatchResult {
    std::vector<Trade> trades;
    std::vector<ExecutionReport> reports;
    std::vector<Rejection> rejections;
};

class MatchingEngine {
public:
    explicit MatchingEngine(EngineConfig config = {});

    MatchResult submit(const OrderEvent& event);

    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
    [[nodiscard]] OrderBook& book() noexcept { return book_; }
    [[nodiscard]] const std::vector<Order>& pending_stops() const noexcept {
        return pending_stops_;
    }
    [[nodiscard]] std::vector<Order>& pending_stops() noexcept { return pending_stops_; }

private:
    OrderBook book_;
    EngineConfig config_;
    uint64_t sequence_{0};
    std::vector<Order> pending_stops_;

    MatchResult process_new_order(const NewOrderEvent& e);
    MatchResult process_cancel(const CancelOrderEvent& e);
    MatchResult process_modify(const ModifyOrderEvent& e);
    MatchResult process_replace(const ReplaceOrderEvent& e);

    // Core matching
    void match_limit(Order& aggressor, MatchResult& result);
    void match_market(Order& aggressor, MatchResult& result);

    // Stop order handling
    void evaluate_stop_triggers(MatchResult& result);
    void process_triggered_stop(Order& stop, MatchResult& result);

    // Iceberg replenishment
    void replenish_iceberg(Order& passive, Quantity filled_qty, MatchResult& result);

    // Fill helpers
    void execute_fill(Order& aggressor, Order& passive, Quantity fill_qty, MatchResult& result);
};

} // namespace lob
