#include "order_book.hpp"
#include <algorithm>
#include <sstream>

std::vector<std::string> OrderBook::processOrder(Side side,
                                                 int qty,
                                                 int64_t price_ticks,
                                                 int64_t order_id,
                                                 const std::function<std::string(int64_t)>& fmt_price) {
    std::vector<std::string> out;
    if (qty <= 0 || price_ticks <= 0) {
        out.emplace_back("ERROR Invalid order");
        return out;
    }

    int remaining = qty;

    if (side == Side::Buy) {
        // Cross into best asks while price crosses
        while (remaining > 0 && !asks_.empty() && asks_.begin()->first <= price_ticks) {
            auto lvl_it = asks_.begin();         // lowest ask level
            auto& lvl = lvl_it->second;          // FIFO at this price
            while (remaining > 0 && !lvl.empty()) {
                auto& resting = lvl.front();
                int trade_qty = std::min(remaining, resting.qty);
                {
                    std::ostringstream oss;
                    oss << "TRADE " << trade_qty << " @ " << fmt_price(lvl_it->first)
                        << " against id " << resting.id;
                    out.push_back(oss.str());
                }
                remaining  -= trade_qty;
                resting.qty -= trade_qty;
                if (resting.qty == 0) lvl.pop_front();
            }
            if (lvl.empty()) asks_.erase(lvl_it);
        }
        if (remaining > 0) {
            auto& lvl = bids_[price_ticks];
            lvl.push_back(RestingOrder{order_id, remaining});
            std::ostringstream oss;
            oss << "ORDER_ADDED BUY " << remaining << " @ " << fmt_price(price_ticks)
                << " id " << order_id;
            out.push_back(oss.str());
        }
    } else { // Sell
        while (remaining > 0 && !bids_.empty() && bids_.begin()->first >= price_ticks) {
            auto lvl_it = bids_.begin();         // highest bid level
            auto& lvl = lvl_it->second;
            while (remaining > 0 && !lvl.empty()) {
                auto& resting = lvl.front();
                int trade_qty = std::min(remaining, resting.qty);
                {
                    std::ostringstream oss;
                    oss << "TRADE " << trade_qty << " @ " << fmt_price(lvl_it->first)
                        << " against id " << resting.id;
                    out.push_back(oss.str());
                }
                remaining  -= trade_qty;
                resting.qty -= trade_qty;
                if (resting.qty == 0) lvl.pop_front();
            }
            if (lvl.empty()) bids_.erase(lvl_it);
        }
        if (remaining > 0) {
            auto& lvl = asks_[price_ticks];
            lvl.push_back(RestingOrder{order_id, remaining});
            std::ostringstream oss;
            oss << "ORDER_ADDED SELL " << remaining << " @ " << fmt_price(price_ticks)
                << " id " << order_id;
            out.push_back(oss.str());
        }
    }

    // Snapshots
    if (!bids_.empty()) {
        std::ostringstream oss; oss << "BEST_BID " << fmt_price(bids_.begin()->first)
                                    << " x " << levelQty(bids_.begin()->second);
        out.push_back(oss.str());
    }
    if (!asks_.empty()) {
        std::ostringstream oss; oss << "BEST_ASK " << fmt_price(asks_.begin()->first)
                                    << " x " << levelQty(asks_.begin()->second);
        out.push_back(oss.str());
    }

    return out;
}