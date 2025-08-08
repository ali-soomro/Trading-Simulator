#include "order_book.hpp"
#include <netinet/tcp.h>   // <-- for TCP_NODELAY
#include <sys/time.h>      // <-- for timeval (SO_RCVTIMEO) on some systems
#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <atomic>

static std::atomic<bool> g_running{true};
static int g_server_fd = -1;

static void handle_sigint(int) {
    g_running = false;
    if (g_server_fd >= 0) { close(g_server_fd); g_server_fd = -1; }
    std::cerr << "\n[Signal] SIGINT received. Shutting down server...\n";
}

// Read one '\n'-terminated line from a blocking socket.
// Returns false on EOF/error. The returned line excludes '\n'.
static bool read_line(int fd, std::string& line) {
    line.clear();
    char ch = 0;
    while (true) {
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n == 0)  return false;         // EOF
        if (n < 0)  { perror("recv"); return false; }
        if (ch == '\n') break;
        line.push_back(ch);
        if (line.size() > 8192) { line.clear(); return false; } // simple guard
    }
    return true;
}

int main() {
    std::signal(SIGINT, handle_sigint);

    // 1) Socket
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // 2) Bind / listen
    sockaddr_in addr{}; addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    if (bind(g_server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(g_server_fd); return 1; }
    if (listen(g_server_fd, 16) < 0) { perror("listen"); close(g_server_fd); return 1; }

    std::cout << "Exchange waiting for connection... (Ctrl-C to quit)\n";

    OrderBook book;

    // 3) Accept loop (single-threaded; multiple orders per connection)
    while (g_running) {
        socklen_t len = sizeof(addr);
        int client_fd = accept(g_server_fd, (sockaddr*)&addr, &len);
        if (client_fd < 0) {
            if (!g_running) break; // interrupted by SIGINT
            perror("accept");
            continue;
        }
        std::cout << "Client connected!\n";

        std::string line;
        while (g_running) {
            if (!read_line(client_fd, line)) { std::cout << "Client disconnected.\n"; break; }
            if (line.empty()) { std::cout << "Empty line -> close.\n"; break; }

            if (line == "QUIT") {
                auto now = std::chrono::high_resolution_clock::now();
                long long ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
                std::ostringstream bye; bye << "ACK " << ts_us << "\nBYE\n";
                std::string payload = bye.str();
                (void)send(client_fd, payload.c_str(), payload.size(), 0);
                std::cout << "Client requested QUIT.\n";
                break;
            }

            // Parse: NEW BUY|SELL <qty> @ <price>
            std::istringstream iss(line);
            std::string type, sideStr; int qty = 0; char at = 0; double price = 0.0;
            iss >> type >> sideStr >> qty >> at >> price;

            // Compose reply with ACK first (client parses timestamp)
            auto now = std::chrono::high_resolution_clock::now();
            long long ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
            std::ostringstream wire; wire << "ACK " << ts_us << "\n";

            if (type != "NEW" || (sideStr != "BUY" && sideStr != "SELL") || at != '@' || qty <= 0 || price <= 0.0) {
                wire << "ERROR Invalid order. Expected: NEW BUY|SELL <qty> @ <price>\n";
            } else {
                Side side = (sideStr == "BUY" ? Side::Buy : Side::Sell);
                auto lines = book.processOrder(side, qty, price);
                for (auto& l : lines) wire << l << "\n";
            }

            std::string payload = wire.str();
            if (send(client_fd, payload.c_str(), payload.size(), 0) < 0) { perror("send"); break; }
        }

        close(client_fd);
    }

    if (g_server_fd >= 0) close(g_server_fd);
    std::cout << "Server shut down.\n";
    return 0;
}