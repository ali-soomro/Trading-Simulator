#include "order_book.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

static bool hasPrefix(const std::vector<std::string>& v, const std::string& p) {
    for (auto& s : v) if (s.rfind(p, 0) == 0) return true; // starts_with
    return false;
}

TEST(OrderBook, AddsBuyWhenNoAsk) {
    OrderBook ob;
    auto r = ob.processOrder(Side::Buy, 100, 50.25);
    EXPECT_TRUE(hasPrefix(r, "ORDER_ADDED BUY 100 @ 50.25"));
    EXPECT_TRUE(ob.hasBestBid());
    EXPECT_DOUBLE_EQ(ob.bestBidPrice(), 50.25);
    EXPECT_EQ(ob.bestBidQty(), 100);
}

TEST(OrderBook, CrossesSellIntoBid) {
    OrderBook ob;
    (void)ob.processOrder(Side::Buy, 100, 50.25);
    auto r = ob.processOrder(Side::Sell, 60, 50.10);
    EXPECT_TRUE(hasPrefix(r, "TRADE 60 @ 50.25"));
    EXPECT_TRUE(ob.hasBestBid());
    EXPECT_EQ(ob.bestBidQty(), 40);
}

TEST(OrderBook, PartialFillCreatesAskResidual) {
    OrderBook ob;
    (void)ob.processOrder(Side::Buy, 50, 50.25);
    auto r = ob.processOrder(Side::Sell, 120, 50.20);
    EXPECT_TRUE(hasPrefix(r, "TRADE 50 @ 50.25"));
    EXPECT_TRUE(hasPrefix(r, "ORDER_ADDED SELL 70 @ 50.2"));
    EXPECT_TRUE(ob.hasBestAsk());
    EXPECT_DOUBLE_EQ(ob.bestAskPrice(), 50.2);
    EXPECT_EQ(ob.bestAskQty(), 70);
}

TEST(OrderBook, AggregatesAtSamePrice) {
    OrderBook ob;
    (void)ob.processOrder(Side::Buy, 100, 50.25);
    (void)ob.processOrder(Side::Buy, 50,  50.25);
    EXPECT_TRUE(ob.hasBestBid());
    EXPECT_DOUBLE_EQ(ob.bestBidPrice(), 50.25);
    EXPECT_EQ(ob.bestBidQty(), 150);
}