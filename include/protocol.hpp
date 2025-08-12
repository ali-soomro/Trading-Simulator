#pragma once
#include "order_book.hpp"
#include <cstdint>

// Type of work item for the engine thread
enum class MsgType { New, Cancel, Modify };

// Work item sent from a network thread to the engine thread.
struct OrderMsg {
    MsgType   type{MsgType::New};

    // For NEW only
    Side      side{Side::Buy};
    int       qty{0};               // NEW (or MOD new_qty)
    int64_t   price_ticks{0};       // NEW (or MOD new_price_ticks)

    // For all types
    int64_t   order_id{0};          // for NEW: server-assigned id; for CXL/MOD: existing id
    int       client_fd{-1};        // where to send response lines
};