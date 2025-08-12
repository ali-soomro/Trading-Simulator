#include "engine_queue.hpp"

OrderQueue::OrderQueue(size_t capacity) : capacity_(capacity) {}

bool OrderQueue::push(const OrderMsg& msg) {
    std::unique_lock<std::mutex> lk(m_);
    cv_not_full_.wait(lk, [&]{ return stop_ || q_.size() < capacity_; });
    if (stop_) return false;
    q_.push(msg);
    cv_not_empty_.notify_one();
    return true;
}

std::optional<OrderMsg> OrderQueue::pop() {
    std::unique_lock<std::mutex> lk(m_);
    cv_not_empty_.wait(lk, [&]{ return stop_ || !q_.empty(); });
    if (q_.empty()) return std::nullopt; // stopped and drained
    OrderMsg m = q_.front();
    q_.pop();
    cv_not_full_.notify_one();
    return m;
}

void OrderQueue::stop() {
    std::lock_guard<std::mutex> lk(m_);
    stop_ = true;
    cv_not_empty_.notify_all();
    cv_not_full_.notify_all();
}