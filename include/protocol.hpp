#pragma once
#include "order_book.hpp"
#include <cstdint>

// Work item sent from a network thread to the engine thread.
struct OrderMsg {
    Side      side;
    int       qty;          // positive
    int64_t   price_ticks;  // integer ticks (e.g., 1 tick = £0.01)
    int64_t   order_id;     // unique ID (server-assigned)
    int       client_fd;    // socket to send book events back to
};