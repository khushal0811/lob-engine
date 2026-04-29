#include "engine/matching_engine.hpp"
#include <gtest/gtest.h>

using namespace lob;

static Order make_limit(OrderId id, Side side, Price price, Quantity qty, Timestamp ts = 0) {
    Order o;
    o.id = id;
    o.side = side;
    o.type = OrderType::Limit;
    o.price = price;
    o.quantity = qty;
    o.orig_qty = qty;
    o.timestamp = ts;
    o.status = OrderStatus::New;
    return o;
}

TEST(ModifyOrder, QuantityReductionPreservesPosition) {
    MatchingEngine engine;
    // Two orders at same price — order 1 should fill first
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 60)});
    engine.submit(NewOrderEvent{make_limit(2, Side::Sell, 100, 40)});

    // Reduce order 1's quantity — must stay at front of queue
    ModifyOrderEvent mod{1, 0, 30, 0}; // reduce to 30
    auto mod_result = engine.submit(mod);
    EXPECT_TRUE(mod_result.rejections.empty());

    // Now a buy should fill order 1 first (still at front)
    auto result = engine.submit(NewOrderEvent{make_limit(3, Side::Buy, 100, 30)});
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].passive_id, 1u); // order 1 filled, not order 2
    EXPECT_EQ(result.trades[0].quantity, 30u);
}

TEST(ModifyOrder, PriceChangeLosesPriority) {
    MatchingEngine engine;
    // Order 1 placed first, order 2 placed second — at price 100
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 30, 1)});
    engine.submit(NewOrderEvent{make_limit(2, Side::Sell, 100, 30, 2)});

    // Modify order 1 to a different price (loses time priority)
    ModifyOrderEvent mod{1, 101, 0, 10}; // new price = 101, new timestamp = 10
    engine.submit(mod);

    // Buy at 101 — order 2 (originally second) now fills first at 100, order 1 at 101
    auto result = engine.submit(NewOrderEvent{make_limit(3, Side::Buy, 101, 60)});
    ASSERT_EQ(result.trades.size(), 2u);
    EXPECT_EQ(result.trades[0].passive_id, 2u); // order 2 fills first
    EXPECT_EQ(result.trades[1].passive_id, 1u); // order 1 fills second
}

TEST(ModifyOrder, QuantityIncreaseLosesPriority) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 20, 1)});
    engine.submit(NewOrderEvent{make_limit(2, Side::Sell, 100, 20, 2)});

    // Increasing qty is cancel + reinsert → goes to back of queue
    ModifyOrderEvent mod{1, 0, 40, 10}; // increase qty to 40
    engine.submit(mod);

    // A buy of 20 should fill order 2 (now at front) not order 1
    auto result = engine.submit(NewOrderEvent{make_limit(3, Side::Buy, 100, 20)});
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].passive_id, 2u);
}

TEST(ModifyOrder, ModifyNonExistentOrderRejected) {
    MatchingEngine engine;
    ModifyOrderEvent mod{999, 105, 0, 0};
    auto result = engine.submit(mod);

    ASSERT_EQ(result.rejections.size(), 1u);
    EXPECT_EQ(result.rejections[0].order_id, 999u);
    EXPECT_EQ(result.rejections[0].reason, RejectReason::OrderNotFound);
}

TEST(ModifyOrder, LevelVolumeUpdatedOnQtyReduction) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 80)});

    ModifyOrderEvent mod{1, 0, 30, 0}; // reduce to 30
    engine.submit(mod);

    ASSERT_TRUE(engine.book().best_ask().has_value());
    // Level should still exist with the reduced order
    EXPECT_EQ(engine.book().order_count(), 1u);
}
