#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <functional>   // std::greater
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <algorithm>    // std::min
#include <atomic>

static std::atomic<bool> g_running{true};
static int g_server_fd = -1;

// ===== Order book (aggregated): price -> total quantity =====
static std::map<double, int, std::greater<double>> bids; // highest price first
static std::map<double, int> asks;                       // lowest price first

// ===== Very simple text protocol =====
//   - "NEW BUY <qty> @ <price>\n"
//   - "NEW SELL <qty> @ <price>\n"
//   - "QUIT\n"  (client asks to close this connection)

static void handle_sigint(int) {
    g_running = false;
    if (g_server_fd >= 0) {
        // Closing the listening socket will unblock accept()
        close(g_server_fd);
        g_server_fd = -1;
    }
    std::cerr << "\n[Signal] SIGINT received. Shutting down server...\n";
}

// Read one '\n'-terminated line from a blocking socket.
// Returns false on EOF/error. The returned line excludes the '\n'.
static bool read_line(int fd, std::string& line) {
    line.clear();
    char ch = 0;
    ssize_t n = 0;
    while (true) {
        n = recv(fd, &ch, 1, 0);
        if (n == 0)  return false;          // EOF (client closed)
        if (n < 0)  { perror("recv"); return false; }
        if (ch == '\n') break;
        line.push_back(ch);
        // Simple guard to avoid unbounded lines
        if (line.size() > 4096) { line.clear(); return false; }
    }
    return true;
}

static std::string process_order(const std::string& msg) {
    std::istringstream iss(msg);
    std::string type, side;
    int qty = 0;
    char at_sign = 0;
    double price = 0.0;

    iss >> type >> side >> qty >> at_sign >> price;

    std::ostringstream out;

    if (type != "NEW" || (side != "BUY" && side != "SELL") || at_sign != '@' || qty <= 0 || price <= 0.0) {
        out << "ERROR Invalid order. Expected: NEW BUY|SELL <qty> @ <price>\n";
        return out.str();
    }

    int remaining = qty;

    if (side == "BUY") {
        // Cross against best asks
        while (remaining > 0 && !asks.empty() && asks.begin()->first <= price) {
            auto bestAsk = asks.begin();
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
        // Cross against best bids
        while (remaining > 0 && !bids.empty() && bids.begin()->first >= price) {
            auto bestBid = bids.begin();
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

    // (Nice for debugging) best bid/ask snapshot
    if (!bids.empty())  out << "BEST_BID " << bids.begin()->first << " x " << bids.begin()->second << "\n";
    if (!asks.empty())  out << "BEST_ASK " << asks.begin()->first << " x " << asks.begin()->second << "\n";

    return out.str();
}

int main() {
    // Install Ctrl-C handler
    std::signal(SIGINT, handle_sigint);

    // 1) Create socket
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd == -1) { perror("socket"); return 1; }

    // Allow quick restarts
    int yes = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // 2) Bind/listen
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    if (bind(g_server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(g_server_fd);
        return 1;
    }
    if (listen(g_server_fd, 8) < 0) {
        perror("listen");
        close(g_server_fd);
        return 1;
    }

    std::cout << "Exchange waiting for connection... (Ctrl-C to quit)\n";

    // Outer loop: keep accepting new clients until SIGINT or fatal error
    while (g_running) {
        socklen_t len = sizeof(addr);
        int client_fd = accept(g_server_fd, (sockaddr*)&addr, &len);
        if (client_fd < 0) {
            if (!g_running) break; // likely interrupted by SIGINT close
            perror("accept");
            continue;
        }
        std::cout << "Client connected!\n";

        // Per-connection loop: multiple orders per connection
        std::string line;
        while (g_running) {
            if (!read_line(client_fd, line)) {
                std::cout << "Client disconnected.\n";
                break;
            }
            if (line.empty()) {
                std::cout << "Empty line received, closing connection.\n";
                break;
            }

            // Handle QUIT command
            if (line == "QUIT") {
                auto now = std::chrono::high_resolution_clock::now();
                long long ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
                std::ostringstream bye;
                bye << "ACK " << ts_us << "\nBYE\n";
                std::string payload = bye.str();
                (void)send(client_fd, payload.c_str(), payload.size(), 0);
                std::cout << "Client requested QUIT. Closing connection.\n";
                break;
            }

            std::cout << "Received: " << line << "\n";

            // Process order (may produce multiple lines)
            std::string book_reply = process_order(line);

            // ALWAYS send ACK <ts> first so the client’s latency parser works
            auto now = std::chrono::high_resolution_clock::now();
            long long ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

            std::ostringstream wire;
            wire << "ACK " << ts_us << "\n";
            wire << book_reply;

            const std::string payload = wire.str();
            if (send(client_fd, payload.c_str(), payload.size(), 0) < 0) {
                perror("send");
                break;
            }
        }

        close(client_fd); // close just this client, keep listening for the next
    }

    if (g_server_fd >= 0) close(g_server_fd);
    std::cout << "Server shut down.\n";
    return 0;
}