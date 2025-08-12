#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

enum class Side { Buy, Sell };

// FIFO per price level
struct RestingOrder {
    int64_t id; // unique order id
    int     qty;
};

class OrderBook {
public:
    // Book operates in integer ticks. Caller provides a formatter (ticks -> string).
    // Emits human-readable events:
    //   TRADE <qty> @ <price_str> against id <id>
    //   ORDER_ADDED BUY|SELL <qty> @ <price_str> id <id>
    //   BEST_BID <price_str> x <qty>
    //   BEST_ASK <price_str> x <qty>
    std::vector<std::string> processOrder(Side side,
                                          int qty,
                                          int64_t price_ticks,
                                          int64_t order_id,
                                          const std::function<std::string(int64_t)>& fmt_price);

    // Diagnostics (engine thread owns the book)
    bool    hasBestBid()   const { return !bids_.empty(); }
    bool    hasBestAsk()   const { return !asks_.empty(); }
    int64_t bestBidTicks() const { return bids_.empty() ? 0 : bids_.begin()->first; }
    int     bestBidQty()   const { return bids_.empty() ? 0 : levelQty(bids_.begin()->second); }
    int64_t bestAskTicks() const { return asks_.empty() ? 0 : asks_.begin()->first; }
    int     bestAskQty()   const { return asks_.empty() ? 0 : levelQty(asks_.begin()->second); }

private:
    using Level = std::deque<RestingOrder>;
    static int levelQty(const Level& lvl) {
        int sum = 0;
        for (const auto& ro : lvl) sum += ro.qty;
        return sum;
    }

    // Price → FIFO of orders; bids highest-first, asks lowest-first
    std::map<int64_t, Level, std::greater<int64_t>> bids_;
    std::map<int64_t, Level>                        asks_;
};