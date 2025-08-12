#include "market_data.hpp"
#include <unistd.h>
#include <cstring>
#include <iostream>

MarketDataPublisher::MarketDataPublisher(const std::string& host, uint16_t port, bool enabled)
: enabled_(enabled) {
    if (!enabled_) return;

    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        perror("socket");
        enabled_ = false;
        return;
    }

    int yes = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    std::memset(&dest_, 0, sizeof(dest_));
    dest_.sin_family = AF_INET;
    dest_.sin_port   = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &dest_.sin_addr) <= 0) {
        std::cerr << "MarketDataPublisher: invalid host " << host << "\n";
        enabled_ = false;
    }
}

MarketDataPublisher::~MarketDataPublisher() {
    if (sock_ >= 0) close(sock_);
}

void MarketDataPublisher::sendLine(const std::string& line) const {
    if (!enabled_ || sock_ < 0) return;
    (void)sendto(sock_, line.data(), line.size(), 0,
                 reinterpret_cast<const sockaddr*>(&dest_), sizeof(dest_));
}