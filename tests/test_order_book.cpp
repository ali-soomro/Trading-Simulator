#include "order_book.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cmath>

// --- Helpers for the new API (integer ticks + formatter) ---
static constexpr int64_t TICK_FACTOR = 100; // 1 tick = £0.01

static int64_t to_ticks(double px) {
    return static_cast<int64_t>(llround(px * static_cast<double>(TICK_FACTOR)));
}

static std::string fmt_price_ticks(int64_t ticks) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(2);
    oss << (static_cast<double>(ticks) / static_cast<double>(TICK_FACTOR));
    return oss.str();
}

static bool starts_with_any(const std::vector<std::string>& v, const std::string& prefix) {
    for (const auto& s : v) {
        if (s.rfind(prefix, 0) == 0) return true; // starts_with
    }
    return false;
}

// -------------------- Core tests --------------------

TEST(OrderBookV2, AddsBuyWhenNoAsk) {
    OrderBook ob;

    auto r = ob.processOrder(
        Side::Buy, 100, to_ticks(50.25), /*order_id=*/1,
        [](int64_t t){ return fmt_price_ticks(t); });

    EXPECT_TRUE(starts_with_any(r, "ORDER_ADDED BUY 100 @ 50.25 id 1"));
    EXPECT_TRUE(starts_with_any(r, "BEST_BID 50.25 x 100"));

    EXPECT_TRUE(ob.hasBestBid());
    EXPECT_EQ(ob.bestBidTicks(), to_ticks(50.25));
    EXPECT_EQ(ob.bestBidQty(), 100);
}

TEST(OrderBookV2, CrossesSellIntoBidFIFO) {
    OrderBook ob;

    (void)ob.processOrder(Side::Buy, 100, to_ticks(50.25), /*id*/1,
                          [](int64_t t){ return fmt_price_ticks(t); });

    auto r = ob.processOrder(Side::Sell, 60, to_ticks(50.10), /*id*/2,
                             [](int64_t t){ return fmt_price_ticks(t); });

    EXPECT_TRUE(starts_with_any(r, "TRADE 60 @ 50.25 against id 1"));
    EXPECT_TRUE(ob.hasBestBid());
    EXPECT_EQ(ob.bestBidTicks(), to_ticks(50.25));
    EXPECT_EQ(ob.bestBidQty(), 40);
}

TEST(OrderBookV2, PartialFillCreatesAskResidualFIFO) {
    OrderBook ob;

    (void)ob.processOrder(Side::Buy, 50, to_ticks(50.25), /*id*/1,
                          [](int64_t t){ return fmt_price_ticks(t); });

    auto r = ob.processOrder(Side::Sell, 120, to_ticks(50.20), /*id*/2,
                             [](int64_t t){ return fmt_price_ticks(t); });

    EXPECT_TRUE(starts_with_any(r, "TRADE 50 @ 50.25 against id 1"));
    EXPECT_TRUE(starts_with_any(r, "ORDER_ADDED SELL 70 @ 50.20 id 2"));
    EXPECT_TRUE(starts_with_any(r, "BEST_ASK 50.20 x 70"));

    EXPECT_TRUE(ob.hasBestAsk());
    EXPECT_EQ(ob.bestAskTicks(), to_ticks(50.20));
    EXPECT_EQ(ob.bestAskQty(), 70);
}

TEST(OrderBookV2, AggregatesPerLevelButFIFOByID) {
    OrderBook ob;

    (void)ob.processOrder(Side::Buy, 100, to_ticks(50.25), /*id*/1,
                          [](int64_t t){ return fmt_price_ticks(t); });
    (void)ob.processOrder(Side::Buy,  50, to_ticks(50.25), /*id*/3,
                          [](int64_t t){ return fmt_price_ticks(t); });

    EXPECT_TRUE(ob.hasBestBid());
    EXPECT_EQ(ob.bestBidTicks(), to_ticks(50.25));
    EXPECT_EQ(ob.bestBidQty(), 150);

    auto r = ob.processOrder(Side::Sell, 120, to_ticks(50.20), /*id*/7,
                             [](int64_t t){ return fmt_price_ticks(t); });

    EXPECT_TRUE(starts_with_any(r, "TRADE 100 @ 50.25 against id 1"));
    EXPECT_TRUE(starts_with_any(r, "TRADE 20 @ 50.25 against id 3"));
    EXPECT_TRUE(starts_with_any(r, "BEST_BID 50.25 x 30"));

    EXPECT_TRUE(ob.hasBestBid());
    EXPECT_EQ(ob.bestBidTicks(), to_ticks(50.25));
    EXPECT_EQ(ob.bestBidQty(), 30);
}

// -------------------- Snapshot-specific tests --------------------

TEST(OrderBookV2, SnapshotsAfterAdditions) {
    OrderBook ob;

    // Add a best bid
    auto r1 = ob.processOrder(Side::Buy, 100, to_ticks(50.25), /*id*/1,
                              [](int64_t t){ return fmt_price_ticks(t); });
    EXPECT_TRUE(starts_with_any(r1, "BEST_BID 50.25 x 100"));

    // Add a resting ask higher than best bid
    auto r2 = ob.processOrder(Side::Sell, 70, to_ticks(50.40), /*id*/2,
                              [](int64_t t){ return fmt_price_ticks(t); });

    // Expect both sides in the snapshot lines
    EXPECT_TRUE(starts_with_any(r2, "BEST_BID 50.25 x 100"));
    EXPECT_TRUE(starts_with_any(r2, "BEST_ASK 50.40 x 70"));

    // Check book state agrees
    EXPECT_TRUE(ob.hasBestBid());
    EXPECT_TRUE(ob.hasBestAsk());
    EXPECT_EQ(ob.bestBidTicks(), to_ticks(50.25));
    EXPECT_EQ(ob.bestBidQty(), 100);
    EXPECT_EQ(ob.bestAskTicks(), to_ticks(50.40));
    EXPECT_EQ(ob.bestAskQty(), 70);
}

TEST(OrderBookV2, SnapshotsUpdateAfterTrades) {
    OrderBook ob;

    // Seed both sides
    (void)ob.processOrder(Side::Buy,  80, to_ticks(50.10), /*id*/1,
                          [](int64_t t){ return fmt_price_ticks(t); });
    (void)ob.processOrder(Side::Sell,120, to_ticks(50.15), /*id*/2,
                          [](int64_t t){ return fmt_price_ticks(t); });

    // Incoming BUY crosses part of the ask level
    auto r = ob.processOrder(Side::Buy, 70, to_ticks(50.20), /*id*/3,
                             [](int64_t t){ return fmt_price_ticks(t); });

    // Expect trade(s) at 50.15 and updated snapshots:
    // Ask reduces from 120 -> 50; bid remains 80 @ 50.10
    EXPECT_TRUE(starts_with_any(r, "TRADE 70 @ 50.15 against id 2"));
    EXPECT_TRUE(starts_with_any(r, "BEST_ASK 50.15 x 50"));
    EXPECT_TRUE(starts_with_any(r, "BEST_BID 50.10 x 80"));

    EXPECT_EQ(ob.bestAskTicks(), to_ticks(50.15));
    EXPECT_EQ(ob.bestAskQty(), 50);
    EXPECT_EQ(ob.bestBidTicks(), to_ticks(50.10));
    EXPECT_EQ(ob.bestBidQty(), 80);
}