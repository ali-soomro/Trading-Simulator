#include "order_book.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cmath>

static constexpr int64_t TICK_FACTOR = 100; // 1 tick = £0.01

static int64_t to_ticks(double px) {
    return static_cast<int64_t>(llround(px * static_cast<double>(TICK_FACTOR)));
}
static std::string fmt_price_ticks(int64_t ticks) {
    std::ostringstream oss; oss.setf(std::ios::fixed); oss.precision(2);
    oss << (static_cast<double>(ticks) / static_cast<double>(TICK_FACTOR));
    return oss.str();
}
static bool starts_with_any(const std::vector<std::string>& v, const std::string& prefix) {
    for (const auto& s : v) if (s.rfind(prefix, 0) == 0) return true;
    return false;
}

// Existing core tests (trimmed for brevity; keep the ones you already added) ...
TEST(OrderBookV2, AddsBuyWhenNoAsk) {
    OrderBook ob;
    auto r = ob.processOrder(Side::Buy, 100, to_ticks(50.25), 1, [](int64_t t){ return fmt_price_ticks(t); });
    EXPECT_TRUE(starts_with_any(r, "ORDER_ADDED BUY 100 @ 50.25 id 1"));
    EXPECT_TRUE(starts_with_any(r, "BEST_BID 50.25 x 100"));
    EXPECT_EQ(ob.bestBidTicks(), to_ticks(50.25));
    EXPECT_EQ(ob.bestBidQty(), 100);
}

TEST(OrderBookV2, CrossesSellIntoBidFIFO) {
    OrderBook ob;
    (void)ob.processOrder(Side::Buy, 100, to_ticks(50.25), 1, [](int64_t t){ return fmt_price_ticks(t); });
    auto r = ob.processOrder(Side::Sell, 60, to_ticks(50.10), 2, [](int64_t t){ return fmt_price_ticks(t); });
    EXPECT_TRUE(starts_with_any(r, "TRADE 60 @ 50.25 against id 1"));
    EXPECT_EQ(ob.bestBidQty(), 40);
}

TEST(OrderBookV2, PartialFillCreatesAskResidualFIFO) {
    OrderBook ob;
    (void)ob.processOrder(Side::Buy, 50, to_ticks(50.25), 1, [](int64_t t){ return fmt_price_ticks(t); });
    auto r = ob.processOrder(Side::Sell, 120, to_ticks(50.20), 2, [](int64_t t){ return fmt_price_ticks(t); });
    EXPECT_TRUE(starts_with_any(r, "TRADE 50 @ 50.25 against id 1"));
    EXPECT_TRUE(starts_with_any(r, "ORDER_ADDED SELL 70 @ 50.20 id 2"));
    EXPECT_TRUE(starts_with_any(r, "BEST_ASK 50.20 x 70"));
    EXPECT_EQ(ob.bestAskTicks(), to_ticks(50.20));
    EXPECT_EQ(ob.bestAskQty(), 70);
}

TEST(OrderBookV2, AggregatesPerLevelButFIFOByID) {
    OrderBook ob;
    (void)ob.processOrder(Side::Buy, 100, to_ticks(50.25), 1, [](int64_t t){ return fmt_price_ticks(t); });
    (void)ob.processOrder(Side::Buy, 50, to_ticks(50.25), 3, [](int64_t t){ return fmt_price_ticks(t); });
    auto r = ob.processOrder(Side::Sell, 120, to_ticks(50.20), 7, [](int64_t t){ return fmt_price_ticks(t); });
    EXPECT_TRUE(starts_with_any(r, "TRADE 100 @ 50.25 against id 1"));
    EXPECT_TRUE(starts_with_any(r, "TRADE 20 @ 50.25 against id 3"));
    EXPECT_TRUE(starts_with_any(r, "BEST_BID 50.25 x 30"));
    EXPECT_EQ(ob.bestBidQty(), 30);
}

// --- New: Cancel ---
TEST(OrderBookV2, CancelRemovesOrderAndUpdatesSnapshot) {
    OrderBook ob;
    (void)ob.processOrder(Side::Buy, 100, to_ticks(50.25), 10, [](int64_t t){ return fmt_price_ticks(t); });
    auto cx = ob.cancel(10, [](int64_t t){ return fmt_price_ticks(t); });
    EXPECT_TRUE(starts_with_any(cx, "CANCELED id 10"));
    // After cancel, no best bid
    EXPECT_FALSE(ob.hasBestBid());
}

// --- New: Replace ---
TEST(OrderBookV2, ReplaceMovesOrderAndCanTrade) {
    OrderBook ob;
    // Rest ask 100 @ 50.40 (id 20)
    (void)ob.processOrder(Side::Sell, 100, to_ticks(50.40), 20, [](int64_t t){ return fmt_price_ticks(t); });
    // Replace ask id 20 -> qty 100 @ 50.10 (crosses bid after we add one)
    (void)ob.processOrder(Side::Buy, 80, to_ticks(50.15), 11, [](int64_t t){ return fmt_price_ticks(t); });

    auto rep = ob.replace(20, 100, to_ticks(50.10), 21, [](int64_t t){ return fmt_price_ticks(t); });
    // Should cancel old, emit REPLACED, then trade 80 @ 50.15 vs id 11, and rest remaining at 50.10
    EXPECT_TRUE(starts_with_any(rep, "CANCELED id 20"));
    EXPECT_TRUE(starts_with_any(rep, "REPLACED 20 -> 21"));
    EXPECT_TRUE(starts_with_any(rep, "TRADE 80 @ 50.15 against id 11"));
    EXPECT_TRUE(starts_with_any(rep, "ORDER_ADDED SELL 20 @ 50.10 id 21"));
    EXPECT_TRUE(starts_with_any(rep, "BEST_ASK 50.10 x 20"));
}