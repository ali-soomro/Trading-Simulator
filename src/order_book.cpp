#include "order_book.hpp"
#include <algorithm>
#include <sstream>

std::vector<std::string> OrderBook::processOrder(Side side, int qty, double price) {
    std::vector<std::string> out;
    if (qty <= 0 || price <= 0.0) {
        out.emplace_back("ERROR Invalid order");
        return out;
    }

    int remaining = qty;

    if (side == Side::Buy) {
        // Cross against asks while price crosses
        while (remaining > 0 && !asks_.empty() && asks_.begin()->first <= price) {
            auto bestAsk = asks_.begin();
            int trade_qty = std::min(remaining, bestAsk->second);
            {
                std::ostringstream oss;
                oss << "TRADE " << trade_qty << " @ " << bestAsk->first;
                out.push_back(oss.str());
            }
            remaining -= trade_qty;
            bestAsk->second -= trade_qty;
            if (bestAsk->second == 0) asks_.erase(bestAsk);
        }
        if (remaining > 0) {
            bids_[price] += remaining;
            std::ostringstream oss;
            oss << "ORDER_ADDED BUY " << remaining << " @ " << price;
            out.push_back(oss.str());
        }
    } else { // Sell
        while (remaining > 0 && !bids_.empty() && bids_.begin()->first >= price) {
            auto bestBid = bids_.begin();
            int trade_qty = std::min(remaining, bestBid->second);
            {
                std::ostringstream oss;
                oss << "TRADE " << trade_qty << " @ " << bestBid->first;
                out.push_back(oss.str());
            }
            remaining -= trade_qty;
            bestBid->second -= trade_qty;
            if (bestBid->second == 0) bids_.erase(bestBid);
        }
        if (remaining > 0) {
            asks_[price] += remaining;
            std::ostringstream oss;
            oss << "ORDER_ADDED SELL " << remaining << " @ " << price;
            out.push_back(oss.str());
        }
    }

    if (!bids_.empty()) {
        std::ostringstream oss; oss << "BEST_BID " << bids_.begin()->first << " x " << bids_.begin()->second;
        out.push_back(oss.str());
    }
    if (!asks_.empty()) {
        std::ostringstream oss; oss << "BEST_ASK " << asks_.begin()->first << " x " << asks_.begin()->second;
        out.push_back(oss.str());
    }
    return out;
}