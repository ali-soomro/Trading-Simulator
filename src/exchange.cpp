#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <functional>   // std::greater
#include <iostream>
#include <map>
#include <sstream>
#include <unistd.h>
#include <algorithm>    // std::min

// Aggregated order book: price -> total quantity
static std::map<double, int, std::greater<double>> bids; // highest price first
static std::map<double, int> asks;                       // lowest price first

// Very simple text protocol: "NEW BUY <qty> @ <price>\n" or "NEW SELL <qty> @ <price>\n"
static std::string process_order(const std::string& msg) {
    std::istringstream iss(msg);
    std::string type, side;
    int qty = 0;
    char at_sign = 0;
    double price = 0.0;

    iss >> type >> side >> qty >> at_sign >> price;

    std::ostringstream out;

    if (type != "NEW" || (side != "BUY" && side != "SELL") || at_sign != '@' || qty <= 0 || price <= 0.0) {
        out << "ERROR Invalid order format. Expected: NEW BUY|SELL <qty> @ <price>\n";
        return out.str();
    }

    int remaining = qty;

    if (side == "BUY") {
        // Match against best asks while price crosses
        while (remaining > 0 && !asks.empty() && asks.begin()->first <= price) {
            auto bestAsk = asks.begin(); // lowest ask
            int trade_qty = std::min(remaining, bestAsk->second);
            out << "TRADE " << trade_qty << " @ " << bestAsk->first << "\n";
            remaining -= trade_qty;
            bestAsk->second -= trade_qty;
            if (bestAsk->second == 0) asks.erase(bestAsk);
        }
        if (remaining > 0) {
            bids[price] += remaining;
            out << "ORDER_ADDED BUY " << remaining << " @ " << price << "\n";
        }
    } else { // SELL
        // Match against best bids while price crosses
        while (remaining > 0 && !bids.empty() && bids.begin()->first >= price) {
            auto bestBid = bids.begin(); // highest bid
            int trade_qty = std::min(remaining, bestBid->second);
            out << "TRADE " << trade_qty << " @ " << bestBid->first << "\n";
            remaining -= trade_qty;
            bestBid->second -= trade_qty;
            if (bestBid->second == 0) bids.erase(bestBid);
        }
        if (remaining > 0) {
            asks[price] += remaining;
            out << "ORDER_ADDED SELL " << remaining << " @ " << price << "\n";
        }
    }

    return out.str();
}

int main() {
    // 1) Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) { perror("socket"); return 1; }

    // Allow quick restarts
    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // 2) Bind/listen
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(server_fd); return 1; }
    if (listen(server_fd, 3) < 0) { perror("listen"); close(server_fd); return 1; }

    std::cout << "Exchange waiting for connection...\n";

    // 3) Accept one client (simple demo)
    socklen_t len = sizeof(addr);
    int client_fd = accept(server_fd, (sockaddr*)&addr, &len);
    if (client_fd < 0) { perror("accept"); close(server_fd); return 1; }
    std::cout << "Client connected!\n";

    // 4) Read one message
    char buf[1024] = {0};
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) { perror("read"); close(client_fd); close(server_fd); return 1; }

    std::string request(buf);
    std::cout << "Received: " << request;

    // 5) Process order → may produce multiple lines (TRADE/ORDER_ADDED)
    std::string book_reply = process_order(request);

    // 6) ALWAYS send ACK <ts> FIRST so the current client can parse a timestamp
    auto now = std::chrono::high_resolution_clock::now();
    long long ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    std::ostringstream wire;
    wire << "ACK " << ts_us << "\n";  // first line for latency parsing
    wire << book_reply;                // then any book messages

    // 7) Send response
    std::string payload = wire.str();
    if (send(client_fd, payload.c_str(), payload.size(), 0) < 0) {
        perror("send");
    }

    // 8) Close
    close(client_fd);
    close(server_fd);
    return 0;
}