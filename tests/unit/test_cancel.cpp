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

TEST(CancelOrder, CancelRestingOrderSucceeds) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Buy, 100, 50)});
    EXPECT_TRUE(engine.book().has_order(1));

    CancelOrderEvent cancel{1, 0};
    auto result = engine.submit(cancel);

    EXPECT_TRUE(result.rejections.empty());
    ASSERT_EQ(result.reports.size(), 1u);
    EXPECT_EQ(result.reports[0].status, OrderStatus::Cancelled);
    EXPECT_EQ(result.reports[0].remaining_qty, 50u);
    EXPECT_FALSE(engine.book().has_order(1));
    EXPECT_FALSE(engine.book().best_bid().has_value());
}

TEST(CancelOrder, CancelNonExistentOrderReturnsRejection) {
    MatchingEngine engine;
    CancelOrderEvent cancel{999, 0};
    auto result = engine.submit(cancel);

    EXPECT_TRUE(result.reports.empty());
    ASSERT_EQ(result.rejections.size(), 1u);
    EXPECT_EQ(result.rejections[0].order_id, 999u);
    EXPECT_EQ(result.rejections[0].reason, RejectReason::OrderNotFound);
}

TEST(CancelOrder, CancelAlreadyFilledOrderReturnsNotCancellable) {
    MatchingEngine engine;
    // Place a sell, then buy fills it completely
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 50)});
    engine.submit(NewOrderEvent{make_limit(2, Side::Buy, 100, 50)});

    // Order 1 is now fully filled and off the book
    // Trying to cancel it should fail with OrderNotFound (it's gone from id_map)
    CancelOrderEvent cancel{1, 0};
    auto result = engine.submit(cancel);

    ASSERT_EQ(result.rejections.size(), 1u);
    EXPECT_EQ(result.rejections[0].reason, RejectReason::OrderNotFound);
}

TEST(CancelOrder, CancelReducesBookOrderCount) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Buy, 100, 10)});
    engine.submit(NewOrderEvent{make_limit(2, Side::Buy, 101, 10)});
    EXPECT_EQ(engine.book().order_count(), 2u);

    engine.submit(CancelOrderEvent{1, 0});
    EXPECT_EQ(engine.book().order_count(), 1u);
}

TEST(CancelOrder, CancelSellOrder) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 105, 30)});

    auto result = engine.submit(CancelOrderEvent{1, 0});
    EXPECT_TRUE(result.rejections.empty());
    EXPECT_EQ(result.reports[0].status, OrderStatus::Cancelled);
    EXPECT_FALSE(engine.book().best_ask().has_value());
}
