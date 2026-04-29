#include "engine/matching_engine.hpp"
#include <gtest/gtest.h>

using namespace lob;

static Order make_limit(OrderId id, Side side, Price price, Quantity qty) {
    Order o;
    o.id = id;
    o.side = side;
    o.type = OrderType::Limit;
    o.price = price;
    o.quantity = qty;
    o.orig_qty = qty;
    o.status = OrderStatus::New;
    return o;
}

static Order make_iceberg(OrderId id, Side side, Price price, Quantity total_qty, Quantity peak) {
    Order o;
    o.id = id;
    o.side = side;
    o.type = OrderType::Iceberg;
    o.price = price;
    o.quantity = total_qty; // engine will split into peak + reserve
    o.orig_qty = total_qty;
    o.peak_qty = peak;
    o.status = OrderStatus::New;
    return o;
}

// ---------------------------------------------------------------------------
// Only peak_qty is visible on the book
// ---------------------------------------------------------------------------
TEST(IcebergOrders, OnlyPeakVisibleOnBook) {
    MatchingEngine engine;
    // Iceberg sell: total 100, peak 20
    engine.submit(NewOrderEvent{make_iceberg(1, Side::Sell, 100, 100, 20)});

    ASSERT_TRUE(engine.book().best_ask().has_value());
    EXPECT_EQ(*engine.book().best_ask(), 100);

    // Level volume should be peak only (20), not 100
    const auto& asks = engine.book().asks();
    auto it = asks.find(100);
    ASSERT_NE(it, asks.end());
    EXPECT_EQ(it->second.total_volume(), 20u);
}

// ---------------------------------------------------------------------------
// Replenishment: peak exhausted → refilled from reserve
// ---------------------------------------------------------------------------
TEST(IcebergOrders, PeakExhaustedThenReplenished) {
    MatchingEngine engine;
    // Iceberg sell: total 60, peak 20 → reserve 40
    engine.submit(NewOrderEvent{make_iceberg(1, Side::Sell, 100, 60, 20)});

    // Buy 20 — fills peak, reserve should replenish
    auto result = engine.submit(NewOrderEvent{make_limit(2, Side::Buy, 100, 20)});
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 20u);

    // Iceberg still on book with new peak (20 from reserve 40)
    EXPECT_TRUE(engine.book().best_ask().has_value());
    EXPECT_EQ(engine.book().order_count(), 1u);
    const auto& asks = engine.book().asks();
    auto it = asks.find(100);
    EXPECT_EQ(it->second.total_volume(), 20u); // new peak visible
}

// ---------------------------------------------------------------------------
// After replenishment, iceberg is at back of queue (FIFO resets)
// ---------------------------------------------------------------------------
TEST(IcebergOrders, ReplenishmentResetsTimePriority) {
    MatchingEngine engine;
    // Iceberg sell: total 40, peak 20 → after first fill, goes to back
    engine.submit(NewOrderEvent{make_iceberg(1, Side::Sell, 100, 40, 20)});
    // Normal sell behind it
    engine.submit(NewOrderEvent{make_limit(2, Side::Sell, 100, 20)});

    // Fill the iceberg's first peak (20) → it replenishes, moves to back
    engine.submit(NewOrderEvent{make_limit(3, Side::Buy, 100, 20)});

    // Now buy 20 more — should hit order 2 (now at front), not iceberg (at back)
    auto result = engine.submit(NewOrderEvent{make_limit(4, Side::Buy, 100, 20)});
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].passive_id, 2u); // order 2 fills, not iceberg
}

// ---------------------------------------------------------------------------
// Full exhaustion: peak + reserve both filled → order removed
// ---------------------------------------------------------------------------
TEST(IcebergOrders, FullExhaustionRemovesOrder) {
    MatchingEngine engine;
    // Iceberg sell: total 40, peak 20 → 2 replenishments then gone
    engine.submit(NewOrderEvent{make_iceberg(1, Side::Sell, 100, 40, 20)});

    // Fill all 40 in one aggressive buy
    auto result = engine.submit(NewOrderEvent{make_limit(2, Side::Buy, 100, 40)});

    EXPECT_GE(result.trades.size(), 1u);
    // Total filled qty across trades should be 40
    Quantity total = 0;
    for (const auto& t : result.trades)
        total += t.quantity;
    EXPECT_EQ(total, 40u);

    // Book should be empty
    EXPECT_FALSE(engine.book().best_ask().has_value());
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// ---------------------------------------------------------------------------
// Aggressor sweeps through multiple replenishments in one shot
// ---------------------------------------------------------------------------
TEST(IcebergOrders, AggressorFillsMultipleReplenishments) {
    MatchingEngine engine;
    // Iceberg sell: total 60, peak 20 → 3 peaks of 20
    engine.submit(NewOrderEvent{make_iceberg(1, Side::Sell, 100, 60, 20)});

    auto result = engine.submit(NewOrderEvent{make_limit(2, Side::Buy, 100, 60)});

    Quantity total = 0;
    for (const auto& t : result.trades)
        total += t.quantity;
    EXPECT_EQ(total, 60u);
    EXPECT_FALSE(engine.book().best_ask().has_value());
}

// ---------------------------------------------------------------------------
// Two icebergs at same level: FIFO respected, first one fills before second
// ---------------------------------------------------------------------------
TEST(IcebergOrders, TwoIcebergsFifoRespected) {
    MatchingEngine engine;
    // Two icebergs at 100, each peak 20, total 40
    engine.submit(NewOrderEvent{make_iceberg(1, Side::Sell, 100, 40, 20)});
    engine.submit(NewOrderEvent{make_iceberg(2, Side::Sell, 100, 40, 20)});

    // Buy 20 — should fill iceberg 1's peak first
    auto result = engine.submit(NewOrderEvent{make_limit(3, Side::Buy, 100, 20)});
    ASSERT_GE(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].passive_id, 1u);
}
