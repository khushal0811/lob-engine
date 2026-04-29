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

TEST(ReplaceOrder, ReplaceExistingOrderRemovesOldInsertsNew) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Buy, 100, 50)});
    EXPECT_TRUE(engine.book().has_order(1));

    ReplaceOrderEvent rep{1, make_limit(2, Side::Buy, 99, 30)};
    auto result = engine.submit(rep);

    EXPECT_TRUE(result.trades.empty());
    EXPECT_TRUE(result.rejections.empty());
    // Old order gone
    EXPECT_FALSE(engine.book().has_order(1));
    // New order on book
    EXPECT_TRUE(engine.book().has_order(2));
    EXPECT_EQ(*engine.book().best_bid(), 99);
}

TEST(ReplaceOrder, ReplaceWithImmediatelyMatchingNewOrder) {
    MatchingEngine engine;
    // Resting ask at 100
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 50)});
    // Resting bid at 95 — we'll replace it with a bid at 100
    engine.submit(NewOrderEvent{make_limit(2, Side::Buy, 95, 50)});

    ReplaceOrderEvent rep{2, make_limit(3, Side::Buy, 100, 50)};
    auto result = engine.submit(rep);

    // Should produce a trade
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].price, 100);
    EXPECT_EQ(result.trades[0].quantity, 50u);
    EXPECT_EQ(result.trades[0].aggressor_id, 3u);
    // Book should be empty after full match
    EXPECT_FALSE(engine.book().best_bid().has_value());
    EXPECT_FALSE(engine.book().best_ask().has_value());
}

TEST(ReplaceOrder, ReplaceNonExistentOrderRejectsAndNoNewOrder) {
    MatchingEngine engine;
    ReplaceOrderEvent rep{999, make_limit(2, Side::Buy, 100, 30)};
    auto result = engine.submit(rep);

    ASSERT_EQ(result.rejections.size(), 1u);
    EXPECT_EQ(result.rejections[0].order_id, 999u);
    EXPECT_EQ(result.rejections[0].reason, RejectReason::OrderNotFound);
    // New order must NOT have been inserted
    EXPECT_FALSE(engine.book().has_order(2));
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST(ReplaceOrder, ReplacePreservesOtherOrdersOnBook) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Buy, 100, 20)});
    engine.submit(NewOrderEvent{make_limit(2, Side::Buy, 99, 20)});

    // Replace order 1 only
    ReplaceOrderEvent rep{1, make_limit(3, Side::Buy, 98, 15)};
    engine.submit(rep);

    EXPECT_FALSE(engine.book().has_order(1));
    EXPECT_TRUE(engine.book().has_order(2));
    EXPECT_TRUE(engine.book().has_order(3));
    EXPECT_EQ(engine.book().order_count(), 2u);
    EXPECT_EQ(*engine.book().best_bid(), 99); // order 2 is still best
}
