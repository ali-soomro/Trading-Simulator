#include "order_book.hpp"
#include <algorithm>
#include <sstream>

void OrderBook::refreshSnapshots(std::vector<std::string>& out,
                                 const std::function<std::string(int64_t)>& fmt_price) const {
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
}

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
        while (remaining > 0 && !asks_.empty() && asks_.begin()->first <= price_ticks) {
            auto lvl_it = asks_.begin();
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
                if (resting.qty == 0) {
                    index_.erase(resting.id);
                    lvl.pop_front();
                }
            }
            if (lvl.empty()) asks_.erase(lvl_it);
        }
        if (remaining > 0) {
            auto& lvl = bids_[price_ticks];
            lvl.push_back(RestingOrder{order_id, remaining});
            index_[order_id] = {Side::Buy, price_ticks};
            std::ostringstream oss;
            oss << "ORDER_ADDED BUY " << remaining << " @ " << fmt_price(price_ticks)
                << " id " << order_id;
            out.push_back(oss.str());
        }
    } else { // Sell
        while (remaining > 0 && !bids_.empty() && bids_.begin()->first >= price_ticks) {
            auto lvl_it = bids_.begin();
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
                if (resting.qty == 0) {
                    index_.erase(resting.id);
                    lvl.pop_front();
                }
            }
            if (lvl.empty()) bids_.erase(lvl_it);
        }
        if (remaining > 0) {
            auto& lvl = asks_[price_ticks];
            lvl.push_back(RestingOrder{order_id, remaining});
            index_[order_id] = {Side::Sell, price_ticks};
            std::ostringstream oss;
            oss << "ORDER_ADDED SELL " << remaining << " @ " << fmt_price(price_ticks)
                << " id " << order_id;
            out.push_back(oss.str());
        }
    }

    refreshSnapshots(out, fmt_price);
    return out;
}

// --- renamed helpers ---

bool OrderBook::eraseFromBidsLevel(std::map<int64_t, Level, std::greater<int64_t>>::iterator lvl_it, int64_t id) {
    auto& lvl = lvl_it->second;
    for (auto it = lvl.begin(); it != lvl.end(); ++it) {
        if (it->id == id) {
            lvl.erase(it);
            if (lvl.empty()) bids_.erase(lvl_it);
            return true;
        }
    }
    return false;
}

bool OrderBook::eraseFromAsksLevel(std::map<int64_t, Level>::iterator lvl_it, int64_t id) {
    auto& lvl = lvl_it->second;
    for (auto it = lvl.begin(); it != lvl.end(); ++it) {
        if (it->id == id) {
            lvl.erase(it);
            if (lvl.empty()) asks_.erase(lvl_it);
            return true;
        }
    }
    return false;
}

std::vector<std::string> OrderBook::cancel(int64_t order_id,
                                           const std::function<std::string(int64_t)>& fmt_price) {
    std::vector<std::string> out;
    auto it = index_.find(order_id);
    if (it == index_.end()) {
        out.emplace_back("ERROR Unknown order id " + std::to_string(order_id));
        refreshSnapshots(out, fmt_price);
        return out;
    }
    Side side = it->second.first;
    int64_t px = it->second.second;

    bool removed = false;
    if (side == Side::Buy) {
        auto lvl_it = bids_.find(px);
        if (lvl_it != bids_.end()) removed = eraseFromBidsLevel(lvl_it, order_id);
    } else {
        auto lvl_it = asks_.find(px);
        if (lvl_it != asks_.end()) removed = eraseFromAsksLevel(lvl_it, order_id);
    }
    if (removed) {
        index_.erase(it);
        out.emplace_back("CANCELED id " + std::to_string(order_id));
    } else {
        out.emplace_back("ERROR Unable to cancel id " + std::to_string(order_id));
    }
    refreshSnapshots(out, fmt_price);
    return out;
}

std::vector<std::string> OrderBook::replace(int64_t old_id,
                                            int new_qty,
                                            int64_t new_price_ticks,
                                            int64_t new_id,
                                            const std::function<std::string(int64_t)>& fmt_price) {
    std::vector<std::string> out;

    auto it = index_.find(old_id);
    if (it == index_.end()) {
        out.emplace_back("ERROR Unknown order id " + std::to_string(old_id));
        refreshSnapshots(out, fmt_price);
        return out;
    }
    Side side = it->second.first;

    auto canc = cancel(old_id, fmt_price);
    out.insert(out.end(), canc.begin(), canc.end());
    if (new_qty <= 0 || new_price_ticks <= 0) {
        out.emplace_back("ERROR Invalid replace parameters");
        refreshSnapshots(out, fmt_price);
        return out;
    }
    out.emplace_back("REPLACED " + std::to_string(old_id) + " -> " + std::to_string(new_id));

    auto add = processOrder(side, new_qty, new_price_ticks, new_id, fmt_price);
    out.insert(out.end(), add.begin(), add.end());
    return out;
}