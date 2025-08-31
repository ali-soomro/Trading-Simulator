#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

enum class Side { Buy, Sell };

// FIFO per price level
struct RestingOrder {
    int64_t id; // unique order id
    int     qty;
};

class OrderBook {
public:
    void clear();

    // Add resting liquidity without matching (admin/seed path)
    std::vector<std::string> seed(Side side,
                                  int qty,
                                  int64_t price_ticks,
                                  int64_t order_id,
                                  const std::function<std::string(int64_t)>& fmt_price);

    // Book operates in integer ticks. Caller provides a formatter (ticks -> string).
    std::vector<std::string> processOrder(Side side,
                                          int qty,
                                          int64_t price_ticks,
                                          int64_t order_id,
                                          const std::function<std::string(int64_t)>& fmt_price);

    // Cancel / Replace
    std::vector<std::string> cancel(int64_t order_id,
                                    const std::function<std::string(int64_t)>& fmt_price);

    std::vector<std::string> replace(int64_t old_id,
                                     int new_qty,
                                     int64_t new_price_ticks,
                                     int64_t new_id,
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

    void refreshSnapshots(std::vector<std::string>& out,
                          const std::function<std::string(int64_t)>& fmt_price) const;

    // Distinct helpers to avoid overload ambiguity
    bool eraseFromBidsLevel(std::map<int64_t, Level, std::greater<int64_t>>::iterator lvl_it, int64_t id);
    bool eraseFromAsksLevel(std::map<int64_t, Level>::iterator lvl_it, int64_t id);

    // Index of id -> (side, price_ticks)
    std::unordered_map<int64_t, std::pair<Side,int64_t>> index_;

    // Price → FIFO of orders; bids highest-first, asks lowest-first
    std::map<int64_t, Level, std::greater<int64_t>> bids_;
    std::map<int64_t, Level>                        asks_;
};