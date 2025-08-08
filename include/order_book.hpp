#pragma once
#include <map>
#include <string>
#include <vector>
#include <functional>

enum class Side { Buy, Sell };

class OrderBook {
public:
    // Process one order and return human-readable events:
    // TRADE ..., ORDER_ADDED ..., BEST_BID ..., BEST_ASK ...
    std::vector<std::string> processOrder(Side side, int qty, double price);

    // Diagnostics
    bool   hasBestBid()  const { return !bids_.empty(); }
    bool   hasBestAsk()  const { return !asks_.empty(); }
    double bestBidPrice() const { return bids_.empty() ? 0.0 : bids_.begin()->first; }
    int    bestBidQty()   const { return bids_.empty() ? 0   : bids_.begin()->second; }
    double bestAskPrice() const { return asks_.empty() ? 0.0 : asks_.begin()->first; }
    int    bestAskQty()   const { return asks_.empty() ? 0   : asks_.begin()->second; }

private:
    // Aggregated book: price -> total quantity
    std::map<double, int, std::greater<double>> bids_; // highest price first
    std::map<double, int> asks_;                       // lowest price first
};