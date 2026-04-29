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

static Order make_market(OrderId id, Side side, Quantity qty) {
    Order o;
    o.id = id;
    o.side = side;
    o.type = OrderType::Market;
    o.price = 0;
    o.quantity = qty;
    o.orig_qty = qty;
    o.status = OrderStatus::New;
    return o;
}

TEST(MatchingEngineMarket, BuyFillsCompletelyAgainstRestingAsk) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 50)});

    auto result = engine.submit(NewOrderEvent{make_market(2, Side::Buy, 50)});

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].price, 100);
    EXPECT_EQ(result.trades[0].quantity, 50u);
    EXPECT_EQ(result.trades[0].passive_id, 1u);
    EXPECT_EQ(result.trades[0].aggressor_id, 2u);
    EXPECT_EQ(result.trades[0].aggressor_side, AggressorSide::Buy);
    EXPECT_FALSE(engine.book().best_ask().has_value());
}

TEST(MatchingEngineMarket, BuySweepsMultipleLevels) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 10)});
    engine.submit(NewOrderEvent{make_limit(2, Side::Sell, 101, 10)});
    engine.submit(NewOrderEvent{make_limit(3, Side::Sell, 102, 10)});

    auto result = engine.submit(NewOrderEvent{make_market(4, Side::Buy, 30)});

    ASSERT_EQ(result.trades.size(), 3u);
    EXPECT_EQ(result.trades[0].price, 100);
    EXPECT_EQ(result.trades[1].price, 101);
    EXPECT_EQ(result.trades[2].price, 102);
    EXPECT_TRUE(result.rejections.empty());
    EXPECT_FALSE(engine.book().best_ask().has_value());
}

TEST(MatchingEngineMarket, BuyOnEmptyBookIsRejected) {
    MatchingEngine engine;
    auto result = engine.submit(NewOrderEvent{make_market(1, Side::Buy, 50)});

    EXPECT_TRUE(result.trades.empty());
    ASSERT_EQ(result.rejections.size(), 1u);
    EXPECT_EQ(result.rejections[0].order_id, 1u);
    EXPECT_EQ(result.rejections[0].reason, RejectReason::MarketExhausted);
}

TEST(MatchingEngineMarket, BuyLargerThanLiquidityPartialFillThenRejected) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 20)});

    auto result = engine.submit(NewOrderEvent{make_market(2, Side::Buy, 50)});

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 20u);
    // Residual 30 should produce MarketExhausted
    ASSERT_EQ(result.rejections.size(), 1u);
    EXPECT_EQ(result.rejections[0].reason, RejectReason::MarketExhausted);
    EXPECT_FALSE(engine.book().best_ask().has_value());
}

TEST(MatchingEngineMarket, SellFillsCompletelyAgainstRestingBid) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Buy, 100, 50)});

    auto result = engine.submit(NewOrderEvent{make_market(2, Side::Sell, 50)});

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].price, 100);
    EXPECT_EQ(result.trades[0].quantity, 50u);
    EXPECT_EQ(result.trades[0].aggressor_side, AggressorSide::Sell);
    EXPECT_FALSE(engine.book().best_bid().has_value());
}

TEST(MatchingEngineMarket, SellSweepsMultipleLevels) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_limit(1, Side::Buy, 102, 10)});
    engine.submit(NewOrderEvent{make_limit(2, Side::Buy, 101, 10)});
    engine.submit(NewOrderEvent{make_limit(3, Side::Buy, 100, 10)});

    auto result = engine.submit(NewOrderEvent{make_market(4, Side::Sell, 30)});

    ASSERT_EQ(result.trades.size(), 3u);
    EXPECT_EQ(result.trades[0].price, 102);  // fills highest bid first
    EXPECT_EQ(result.trades[1].price, 101);
    EXPECT_EQ(result.trades[2].price, 100);
    EXPECT_FALSE(engine.book().best_bid().has_value());
}

TEST(MatchingEngineMarket, SellOnEmptyBookIsRejected) {
    MatchingEngine engine;
    auto result = engine.submit(NewOrderEvent{make_market(1, Side::Sell, 50)});

    EXPECT_TRUE(result.trades.empty());
    ASSERT_EQ(result.rejections.size(), 1u);
    EXPECT_EQ(result.rejections[0].reason, RejectReason::MarketExhausted);
}
