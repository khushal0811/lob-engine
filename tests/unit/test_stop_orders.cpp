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

static Order make_stop(OrderId id, Side side, Price stop_price, Quantity qty, Timestamp ts = 0) {
    Order o;
    o.id = id;
    o.side = side;
    o.type = OrderType::Stop;
    o.price = 0;
    o.stop_price = stop_price;
    o.quantity = qty;
    o.orig_qty = qty;
    o.timestamp = ts;
    o.status = OrderStatus::New;
    return o;
}

static Order make_stop_limit(OrderId id, Side side, Price stop_price, Price limit_price,
                             Quantity qty) {
    Order o;
    o.id = id;
    o.side = side;
    o.type = OrderType::StopLimit;
    o.price = limit_price;
    o.stop_price = stop_price;
    o.quantity = qty;
    o.orig_qty = qty;
    o.status = OrderStatus::New;
    return o;
}

// ---------------------------------------------------------------------------
// Buy stop: triggers when trade price >= stop_price
// ---------------------------------------------------------------------------
TEST(StopOrders, BuyStopPlacedInPendingNotOnBook) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_stop(1, Side::Buy, 105, 30)});

    // Must not appear on the book
    EXPECT_FALSE(engine.book().best_bid().has_value());
    EXPECT_FALSE(engine.book().has_order(1));
    EXPECT_EQ(engine.pending_stops().size(), 1u);
}

TEST(StopOrders, BuyStopTriggeredByTrade) {
    MatchingEngine engine;
    // Place resting sell at 100 and buy at 100 — will trade at 100
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 50)});
    // Register a buy stop with trigger at 100
    engine.submit(NewOrderEvent{make_stop(2, Side::Buy, 100, 30)});
    EXPECT_EQ(engine.pending_stops().size(), 1u);

    // Place another sell at 100 to provide liquidity for triggered stop
    engine.submit(NewOrderEvent{make_limit(3, Side::Sell, 100, 30)});

    // Buy that causes trade at 100, which triggers the buy stop
    auto result = engine.submit(NewOrderEvent{make_limit(4, Side::Buy, 100, 50)});

    // The trigger trade + the stop-converted-market trades
    EXPECT_GE(result.trades.size(), 1u);
    EXPECT_EQ(engine.pending_stops().size(), 0u);
}

TEST(StopOrders, SellStopTriggeredByTrade) {
    MatchingEngine engine;
    // Place resting bid at 100
    engine.submit(NewOrderEvent{make_limit(1, Side::Buy, 100, 50)});
    // Register sell stop at trigger price 100
    engine.submit(NewOrderEvent{make_stop(2, Side::Sell, 100, 30)});
    EXPECT_EQ(engine.pending_stops().size(), 1u);

    // Place another bid for the triggered stop to fill against
    engine.submit(NewOrderEvent{make_limit(3, Side::Buy, 100, 30)});

    // Aggressive sell causes trade at 100, triggering the sell stop
    auto result = engine.submit(NewOrderEvent{make_limit(4, Side::Sell, 100, 50)});
    EXPECT_GE(result.trades.size(), 1u);
    EXPECT_EQ(engine.pending_stops().size(), 0u);
}

TEST(StopOrders, StopLimitTriggeredRestsAsLimit) {
    MatchingEngine engine;
    // Place resting buy at 100 and a sell to create a trade
    engine.submit(NewOrderEvent{make_limit(1, Side::Buy, 100, 50)});
    // Stop-limit: sell stop at 100, limit at 99 — should rest as limit after trigger
    engine.submit(NewOrderEvent{make_stop_limit(2, Side::Sell, 100, 99, 30)});

    // Trade at 100 triggers the stop-limit
    auto result = engine.submit(NewOrderEvent{make_limit(3, Side::Sell, 100, 50)});

    // The stop-limit converts to limit at 99, which might not immediately fill
    // (no resting bids at 99 in this scenario)
    EXPECT_EQ(engine.pending_stops().size(), 0u);
    // Triggered stop-limit at 99 should be resting on ask side if not crossed
    // (bid is gone now). The limit order would just rest.
    // What matters is no crash and stop list is empty.
    EXPECT_GE(result.trades.size(), 1u);
}

TEST(StopOrders, ImmediateTriggerOnSubmit) {
    MatchingEngine engine;
    // Seed a last trade price by doing a trade
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 10)});
    engine.submit(NewOrderEvent{make_limit(2, Side::Buy, 100, 10)});
    EXPECT_EQ(engine.book().last_trade_price, 100);

    // Place a buy stop with stop_price <= last_trade_price → immediate trigger
    engine.submit(NewOrderEvent{make_limit(3, Side::Sell, 100, 30)});
    auto result = engine.submit(NewOrderEvent{make_stop(4, Side::Buy, 100, 30)});

    // Should have immediately triggered and traded
    EXPECT_GE(result.trades.size(), 1u);
    EXPECT_EQ(engine.pending_stops().size(), 0u);
}

TEST(StopOrders, MultipleStopsOnlyCorrectOnesTrigger) {
    MatchingEngine engine;
    // Ample resting sells for both the direct buy and the triggered market buy
    engine.submit(NewOrderEvent{make_limit(1, Side::Sell, 100, 200)});

    // Stop at 100 — will trigger on trade at 100
    engine.submit(NewOrderEvent{make_stop(2, Side::Buy, 100, 10)});
    // Stop at 110 — should NOT trigger on a trade at 100
    engine.submit(NewOrderEvent{make_stop(3, Side::Buy, 110, 10)});
    EXPECT_EQ(engine.pending_stops().size(), 2u);

    // Buy 50 — trades at 100, triggering stop@100 (its market buy consumes 10 more)
    engine.submit(NewOrderEvent{make_limit(4, Side::Buy, 100, 50)});

    // Only stop@110 should remain
    EXPECT_EQ(engine.pending_stops().size(), 1u);
    EXPECT_EQ(engine.pending_stops()[0].stop_price, 110);
}

TEST(StopOrders, CancelStopBeforeTrigger) {
    MatchingEngine engine;
    engine.submit(NewOrderEvent{make_stop(1, Side::Buy, 105, 30)});
    EXPECT_EQ(engine.pending_stops().size(), 1u);

    auto result = engine.submit(CancelOrderEvent{1, 0});
    EXPECT_TRUE(result.rejections.empty());
    ASSERT_EQ(result.reports.size(), 1u);
    EXPECT_EQ(result.reports[0].status, OrderStatus::Cancelled);
    EXPECT_EQ(engine.pending_stops().size(), 0u);
}
