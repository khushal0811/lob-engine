#include "book/order_book.hpp"
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
    o.status = OrderStatus::Resting;
    return o;
}

TEST(OrderBook, InsertBuyAppearsBidSide) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 50));
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), 100);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, InsertSellAppearsAskSide) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Sell, 105, 50));
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), 105);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(OrderBook, BestBidIsHighestPrice) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10));
    book.insert_order(make_order(2, Side::Buy, 105, 10));
    book.insert_order(make_order(3, Side::Buy, 98, 10));
    EXPECT_EQ(*book.best_bid(), 105);
}

TEST(OrderBook, BestAskIsLowestPrice) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Sell, 110, 10));
    book.insert_order(make_order(2, Side::Sell, 105, 10));
    book.insert_order(make_order(3, Side::Sell, 115, 10));
    EXPECT_EQ(*book.best_ask(), 105);
}

TEST(OrderBook, CancelOrderRemovesFromBook) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 50));
    EXPECT_TRUE(book.cancel_order(1));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(OrderBook, CancelNonExistentReturnsFalse) {
    OrderBook book;
    EXPECT_FALSE(book.cancel_order(999));
}

TEST(OrderBook, EmptyBookReturnsNullopt) {
    OrderBook book;
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.mid_price().has_value());
    EXPECT_FALSE(book.spread().has_value());
}

TEST(OrderBook, MidPriceAndSpread) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10));
    book.insert_order(make_order(2, Side::Sell, 110, 10));
    ASSERT_TRUE(book.mid_price().has_value());
    EXPECT_EQ(*book.mid_price(), 105);
    ASSERT_TRUE(book.spread().has_value());
    EXPECT_EQ(*book.spread(), 10);
}

TEST(OrderBook, HasOrder) {
    OrderBook book;
    book.insert_order(make_order(42, Side::Buy, 100, 10));
    EXPECT_TRUE(book.has_order(42));
    EXPECT_FALSE(book.has_order(99));
}
