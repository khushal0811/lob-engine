#include "book/price_level.hpp"
#include <gtest/gtest.h>

using namespace lob;

TEST(PriceLevel, PushBackIncreasesVolume) {
    PriceLevel level(100);
    level.push_back(1, 50);
    EXPECT_EQ(level.total_volume(), 50u);
    EXPECT_EQ(level.size(), 1u);
    EXPECT_FALSE(level.empty());
}

TEST(PriceLevel, MultipleOrders) {
    PriceLevel level(100);
    level.push_back(1, 30);
    level.push_back(2, 20);
    EXPECT_EQ(level.total_volume(), 50u);
    EXPECT_EQ(level.size(), 2u);
}

TEST(PriceLevel, PopFrontDecreasesVolumeAndRemovesFront) {
    PriceLevel level(100);
    level.push_back(1, 50);
    level.push_back(2, 30);
    EXPECT_EQ(level.front(), 1u);
    level.pop_front(50);
    EXPECT_EQ(level.front(), 2u);
    EXPECT_EQ(level.total_volume(), 30u);
}

TEST(PriceLevel, RemoveByIdUpdatesVolume) {
    PriceLevel level(100);
    level.push_back(1, 40);
    level.push_back(2, 60);
    level.remove(1, 40);
    EXPECT_EQ(level.total_volume(), 60u);
    EXPECT_EQ(level.size(), 1u);
    EXPECT_EQ(level.front(), 2u);
}

TEST(PriceLevel, EmptyAfterAllRemoved) {
    PriceLevel level(100);
    level.push_back(1, 10);
    level.remove(1, 10);
    EXPECT_TRUE(level.empty());
}

TEST(PriceLevel, FifoOrder) {
    PriceLevel level(100);
    level.push_back(10, 5);
    level.push_back(20, 5);
    level.push_back(30, 5);
    EXPECT_EQ(level.front(), 10u);
    level.pop_front(5);
    EXPECT_EQ(level.front(), 20u);
    level.pop_front(5);
    EXPECT_EQ(level.front(), 30u);
}

TEST(PriceLevel, ReduceFrontVolumeDoesNotPop) {
    PriceLevel level(100);
    level.push_back(1, 50);
    level.reduce_front_volume(20);
    EXPECT_EQ(level.total_volume(), 30u);
    EXPECT_EQ(level.front(), 1u);  // order still there
}
