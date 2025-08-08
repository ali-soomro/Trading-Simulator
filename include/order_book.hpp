#pragma once
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

enum class Side { Buy, Sell };

class OrderBook {
public:
    // Process one order and return human-readable events:
    // TRADE ..., ORDER_ADDED ..., BEST_BID ..., BEST_ASK ...
    std::vector<std::string> processOrder(Side side, int qty, double price);

    // Diagnostics (safe to call; they take the same lock)
    bool   hasBestBid()  const;
    bool   hasBestAsk()  const;
    double bestBidPrice() const;
    int    bestBidQty()   const;
    double bestAskPrice() const;
    int    bestAskQty()   const;

private:
    // Aggregated book: price -> total quantity
    std::map<double, int, std::greater<double>> bids_; // highest price first
    std::map<double, int> asks_;                       // lowest price first

    // Single coarse-grained lock for now
    mutable std::mutex mtx_;
};