#include "engine/matching_engine.hpp"
#include "feed/synthetic_gen.hpp"
#include <gtest/gtest.h>
#include <set>

using namespace lob;

// ---------------------------------------------------------------------------
// Invariant checking helpers
// ---------------------------------------------------------------------------

static void check_orders_exist_in_levels(const OrderBook& book) {
    // Every order ID in a level queue must exist in the id_map
    for (const auto& [price, level] : book.bids()) {
        for (auto id : level.order_ids()) {
            ASSERT_TRUE(book.has_order(id))
                << "Order " << id << " in bid level " << price << " not in id_map";
        }
        // total_volume should not underflow (basic sanity)
        ASSERT_GE(level.total_volume(), 0u)
            << "Negative volume at bid level " << price;
    }
    for (const auto& [price, level] : book.asks()) {
        for (auto id : level.order_ids()) {
            ASSERT_TRUE(book.has_order(id))
                << "Order " << id << " in ask level " << price << " not in id_map";
        }
        ASSERT_GE(level.total_volume(), 0u)
            << "Negative volume at ask level " << price;
    }
}


static void check_bid_ordering(const OrderBook& book) {
    const auto& bids = book.bids();
    if (bids.size() < 2) return;
    auto it = bids.begin();
    Price prev = it->first;
    ++it;
    for (; it != bids.end(); ++it) {
        ASSERT_GT(prev, it->first)
            << "Bid ordering violation: " << prev << " should be > " << it->first;
        prev = it->first;
    }
}

static void check_ask_ordering(const OrderBook& book) {
    const auto& asks = book.asks();
    if (asks.size() < 2) return;
    auto it = asks.begin();
    Price prev = it->first;
    ++it;
    for (; it != asks.end(); ++it) {
        ASSERT_LT(prev, it->first)
            << "Ask ordering violation: " << prev << " should be < " << it->first;
        prev = it->first;
    }
}

static void check_no_crossed_book(const OrderBook& book) {
    auto bb = book.best_bid();
    auto ba = book.best_ask();
    if (bb.has_value() && ba.has_value()) {
        ASSERT_LT(*bb, *ba)
            << "Crossed book: best_bid=" << *bb << " >= best_ask=" << *ba;
    }
}

static void check_id_map_consistency(const OrderBook& book) {
    std::set<OrderId> seen;
    for (const auto& [price, level] : book.bids()) {
        for (auto id : level.order_ids()) {
            ASSERT_TRUE(book.has_order(id))
                << "Order " << id << " in bid level " << price << " not in id_map";
            ASSERT_TRUE(seen.insert(id).second)
                << "Duplicate order " << id << " found across levels";
        }
    }
    for (const auto& [price, level] : book.asks()) {
        for (auto id : level.order_ids()) {
            ASSERT_TRUE(book.has_order(id))
                << "Order " << id << " in ask level " << price << " not in id_map";
            ASSERT_TRUE(seen.insert(id).second)
                << "Duplicate order " << id << " found across levels";
        }
    }
}

static void check_stop_exclusivity(const OrderBook& book,
                                   const std::vector<Order>& pending_stops) {
    for (const auto& stop : pending_stops) {
        ASSERT_FALSE(book.has_order(stop.id))
            << "Stop order " << stop.id << " found in order book — should only be in pending_stops";
    }
}

static void check_all_invariants(const OrderBook& book,
                                 const std::vector<Order>& pending_stops) {
    check_orders_exist_in_levels(book);
    check_bid_ordering(book);
    check_ask_ordering(book);
    check_no_crossed_book(book);
    check_id_map_consistency(book);
    check_stop_exclusivity(book, pending_stops);
}

// ---------------------------------------------------------------------------
// Integration test: 100k events with invariant checks every 1,000
// ---------------------------------------------------------------------------

TEST(Integration, InvariantCheckOver100kEvents) {
    EngineConfig config;
    MatchingEngine engine(config);
    SyntheticGenerator gen(config, 42, 100'000);
    OrderEvent ev;
    uint64_t event_count = 0;

    while (gen.next(ev) && event_count < 100'000) {
        engine.submit(ev);
        ++event_count;
        if (event_count % 1000 == 0) {
            check_all_invariants(engine.book(), engine.pending_stops());
        }
    }

    // Final invariant check at the end
    check_all_invariants(engine.book(), engine.pending_stops());

    EXPECT_GE(event_count, 100'000u)
        << "Generator produced fewer than 100k events";
    SUCCEED() << "Processed " << event_count << " events with 0 invariant violations";
}
