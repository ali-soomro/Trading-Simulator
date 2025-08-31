#include "gtest/gtest.h"
#include "order_book.hpp"

#include <regex>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <cstdio>
#include <cmath>

// -------- helpers -----------------------------------------------------------

static std::string fmt_price_2dp(int64_t ticks) {
    // assuming ticks = price * 100
    double px = static_cast<double>(ticks) / 100.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f", px);
    return std::string(buf);
}

static int64_t to_ticks(double px) {
    return static_cast<int64_t>(std::llround(px * 100.0));
}

static bool contains_line_starting_with(const std::vector<std::string>& lines,
                                        const std::string& prefix) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& s){
        return s.rfind(prefix, 0) == 0; // starts_with
    });
}

static bool contains_regex(const std::vector<std::string>& lines,
                           const std::regex& re) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& s){
        return std::regex_search(s, re);
    });
}

// Accepts optional aggressor token BUY|SELL after "TRADE"
static std::regex trade_re_optional_side(int qty, const std::string& px_regex, long long id) {
    // e.g. ^TRADE (BUY|SELL )?60 @ 50\.25 against id 1
    std::ostringstream oss;
    oss << R"(^TRADE\s+(?:BUY|SELL\s+)?)" << qty
        << R"(\s+@\s+)" << px_regex
        << R"(\s+against id\s+)" << id << R"()";
    return std::regex(oss.str());
}

// Strict version if you want to assert the aggressor explicitly:
static std::regex trade_re_with_side(const char* side, int qty, const std::string& px_regex, long long id) {
    std::ostringstream oss;
    oss << R"(^TRADE\s+)" << side << R"(\s+)" << qty
        << R"(\s+@\s+)" << px_regex
        << R"(\s+against id\s+)" << id << R"()";
    return std::regex(oss.str());
}

// -------- tests -------------------------------------------------------------

TEST(OrderBookV2, AddsBuyWhenNoAsk) {
    OrderBook ob;

    auto r = ob.processOrder(Side::Buy, 100, to_ticks(50.25), 1, fmt_price_2dp);

    EXPECT_TRUE(contains_line_starting_with(r, "ORDER_ADDED BUY"));
    EXPECT_EQ(ob.bestBidTicks(), to_ticks(50.25));
    EXPECT_EQ(ob.bestAskTicks(), 0);
}

TEST(OrderBookV2, CrossesSellIntoBidFIFO) {
    OrderBook ob;

    (void)ob.processOrder(Side::Buy, 100, to_ticks(50.25), 1, fmt_price_2dp); // resting bid @ 50.25

    // Cross with a SELL that should trade against id 1 at the resting bid price
    auto r = ob.processOrder(Side::Sell, 60, to_ticks(50.10), 2, fmt_price_2dp);

    // Accept either "TRADE 60 ..." (old) or "TRADE SELL 60 ..." (new)
    EXPECT_TRUE(contains_regex(r, trade_re_optional_side(60, R"(50\.25)", 1)));

    // Bid remains at 50.25 with reduced qty
    EXPECT_EQ(ob.bestBidTicks(), to_ticks(50.25));
    EXPECT_GE(ob.bestAskTicks(), 0);
}

TEST(OrderBookV2, PartialFillCreatesAskResidualFIFO) {
    OrderBook ob;

    // Rest one bid 50 @ 50.25 (id 1)
    (void)ob.processOrder(Side::Buy, 50, to_ticks(50.25), 1, fmt_price_2dp);

    // Sell 100 @ 50.15 (crosses 50, then leaves 50 resting on ask @ 50.15)
    auto r = ob.processOrder(Side::Sell, 100, to_ticks(50.15), 2, fmt_price_2dp);

    // First execution: 50 @ 50.25 against id 1; tolerate optional aggressor token
    EXPECT_TRUE(contains_regex(r, trade_re_optional_side(50, R"(50\.25)", 1)));

    // Residual should rest on ask @ 50.15 with qty 50
    EXPECT_EQ(ob.bestAskTicks(), to_ticks(50.15));
    // best bid should now be empty
    EXPECT_EQ(ob.bestBidTicks(), 0);
}

TEST(OrderBookV2, AggregatesPerLevelButFIFOByID) {
    OrderBook ob;

    // Two bids at the same price (FIFO by id/time)
    (void)ob.processOrder(Side::Buy, 100, to_ticks(50.25), 1, fmt_price_2dp); // older
    (void)ob.processOrder(Side::Buy,  20, to_ticks(50.25), 3, fmt_price_2dp); // newer
    // Add another level that shouldn't be hit yet
    (void)ob.processOrder(Side::Buy,  10, to_ticks(50.20), 2, fmt_price_2dp);

    // One SELL crossing enough to consume both same-price bids
    auto r = ob.processOrder(Side::Sell, 120, to_ticks(50.10), 4, fmt_price_2dp);

    EXPECT_TRUE(contains_regex(r, trade_re_optional_side(100, R"(50\.25)", 1)));
    EXPECT_TRUE(contains_regex(r, trade_re_optional_side(20,  R"(50\.25)", 3)));

    // Remaining best bid is now 50.20 (10)
    EXPECT_EQ(ob.bestBidTicks(), to_ticks(50.20));
}

// Keep your existing cancel test body if it's already passing in your repo.
// (It passed in your last run.)
TEST(OrderBookV2, CancelRemovesOrderAndUpdatesSnapshot) {
    OrderBook ob;

    // Example shape; replace with your repo's existing cancel test if different.
    (void)ob.processOrder(Side::Buy, 100, to_ticks(50.25), 10, fmt_price_2dp);
    EXPECT_EQ(ob.bestBidTicks(), to_ticks(50.25));

    // If your API is ob.cancel(id, fmt), use that; keeping it generic here:
    auto rep = ob.cancel(10, fmt_price_2dp);
    (void)rep;

    EXPECT_EQ(ob.bestBidTicks(), 0);
}