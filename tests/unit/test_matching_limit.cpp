#include "engine/matching_engine.hpp"
#include <gtest/gtest.h>

using namespace lob;

static Order make_order(OrderId id, Side side, Price price, Quantity qty) {
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

TEST(MatchingEngineLimit, NoMatchBuyBelowAsk) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_order(1, Side::Sell, 105, 50)});
    auto result = engine.submit(NewOrderEvent{make_order(2, Side::Buy, 100, 50)});
    EXPECT_TRUE(result.trades.empty());
    EXPECT_TRUE(engine.book().best_bid().has_value());
    EXPECT_EQ(*engine.book().best_bid(), 100);
}

TEST(MatchingEngineLimit, NoMatchSellAboveBid) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_order(1, Side::Buy, 100, 50)});
    auto result = engine.submit(NewOrderEvent{make_order(2, Side::Sell, 105, 50)});
    EXPECT_TRUE(result.trades.empty());
    EXPECT_TRUE(engine.book().best_ask().has_value());
    EXPECT_EQ(*engine.book().best_ask(), 105);
}

TEST(MatchingEngineLimit, FullMatch) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_order(1, Side::Sell, 100, 50)});
    auto result = engine.submit(NewOrderEvent{make_order(2, Side::Buy, 100, 50)});

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].price, 100);
    EXPECT_EQ(result.trades[0].quantity, 50u);
    EXPECT_EQ(result.trades[0].passive_id, 1u);
    EXPECT_EQ(result.trades[0].aggressor_id, 2u);

    EXPECT_FALSE(engine.book().best_bid().has_value());
    EXPECT_FALSE(engine.book().best_ask().has_value());
}

TEST(MatchingEngineLimit, PartialMatchAggressorSmaller) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_order(1, Side::Sell, 100, 100)});
    auto result = engine.submit(NewOrderEvent{make_order(2, Side::Buy, 100, 40)});

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 40u);

    // Passive order still resting with 60 remaining
    EXPECT_TRUE(engine.book().best_ask().has_value());
    EXPECT_EQ(engine.book().order_count(), 1u);
}

TEST(MatchingEngineLimit, PartialMatchAggressorLarger) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_order(1, Side::Sell, 100, 40)});
    auto result = engine.submit(NewOrderEvent{make_order(2, Side::Buy, 100, 100)});

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 40u);

    // Aggressor remainder should rest on bid side
    EXPECT_TRUE(engine.book().best_bid().has_value());
    EXPECT_EQ(*engine.book().best_bid(), 100);
    EXPECT_FALSE(engine.book().best_ask().has_value());
}

TEST(MatchingEngineLimit, MultiLevelSweep) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_order(1, Side::Sell, 100, 10)});
    engine.submit(NewOrderEvent{make_order(2, Side::Sell, 101, 10)});
    engine.submit(NewOrderEvent{make_order(3, Side::Sell, 102, 10)});

    auto result = engine.submit(NewOrderEvent{make_order(4, Side::Buy, 103, 30)});

    ASSERT_EQ(result.trades.size(), 3u);
    EXPECT_EQ(result.trades[0].price, 100);
    EXPECT_EQ(result.trades[1].price, 101);
    EXPECT_EQ(result.trades[2].price, 102);
    EXPECT_FALSE(engine.book().best_ask().has_value());
    EXPECT_FALSE(engine.book().best_bid().has_value());
}

TEST(MatchingEngineLimit, FifoPriority) {
    MatchingEngine engine;
    // Two orders at same price — first inserted fills first
    engine.submit(NewOrderEvent{make_order(1, Side::Sell, 100, 30)});
    engine.submit(NewOrderEvent{make_order(2, Side::Sell, 100, 30)});

    auto result = engine.submit(NewOrderEvent{make_order(3, Side::Buy, 100, 30)});

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].passive_id, 1u);  // order 1 filled first
    EXPECT_EQ(result.trades[0].quantity, 30u);
    // Order 2 still resting
    EXPECT_TRUE(engine.book().best_ask().has_value());
}

TEST(MatchingEngineLimit, NoBookCrossingInvariant) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_order(1, Side::Buy,  99, 10)});
    engine.submit(NewOrderEvent{make_order(2, Side::Sell, 101, 10)});
    engine.submit(NewOrderEvent{make_order(3, Side::Buy,  98, 5)});
    engine.submit(NewOrderEvent{make_order(4, Side::Sell, 102, 5)});

    auto bb = engine.book().best_bid();
    auto ba = engine.book().best_ask();
    ASSERT_TRUE(bb.has_value());
    ASSERT_TRUE(ba.has_value());
    EXPECT_LT(*bb, *ba);  // no crossed book
}
