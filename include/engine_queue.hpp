#pragma once
#include "protocol.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

// Thread-safe bounded queue for OrderMsg.
// Blocking push/pop with a stop() to wake all waiters and drain.
class OrderQueue {
public:
    explicit OrderQueue(size_t capacity);

    // Blocking push; returns false if queue is stopping.
    bool push(const OrderMsg& msg);

    // Blocking pop; returns empty optional when stopped and drained.
    std::optional<OrderMsg> pop();

    // Request shutdown and wake all waiters.
    void stop();

private:
    size_t capacity_;
    std::queue<OrderMsg> q_;
    std::mutex m_;
    std::condition_variable cv_not_empty_, cv_not_full_;
    bool stop_ = false;
};