#pragma once
#include <string>
#include <cstdint>
#include <arpa/inet.h>

class MarketDataPublisher {
public:
    // If enabled == false, sendLine() is a no‑op.
    MarketDataPublisher(const std::string& host, uint16_t port, bool enabled = true);
    ~MarketDataPublisher();

    // Sends one datagram containing 'line' (no extra '\n' added).
    void sendLine(const std::string& line) const;

    bool enabled() const { return enabled_; }

private:
    int sock_ = -1;
    bool enabled_ = false;
    struct sockaddr_in dest_{};
};