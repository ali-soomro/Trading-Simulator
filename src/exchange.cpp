#include "order_book.hpp"
#include "protocol.hpp"
#include "engine_queue.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <unistd.h>

static std::atomic<bool> g_running{true};
static int g_server_fd = -1;
static std::atomic<int64_t> g_order_id{1};

// Graceful shutdown + ignore SIGPIPE
static void handle_sigint(int) {
    g_running = false;
    if (g_server_fd >= 0) { close(g_server_fd); g_server_fd = -1; }
    std::cerr << "\n[Signal] SIGINT received. Shutting down server...\n";
}

static ssize_t safe_send(int fd, const void* buf, size_t len) {
#ifdef MSG_NOSIGNAL
    return send(fd, buf, len, MSG_NOSIGNAL);
#elif defined(SO_NOSIGPIPE)
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    return send(fd, buf, len, 0);
#else
    return send(fd, buf, len, 0);
#endif
}

// '\n'-terminated line reader; false on EOF/error
static bool read_line(int fd, std::string& line) {
    line.clear();
    char ch = 0;
    while (true) {
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n == 0)  return false;         // EOF
        if (n < 0)  { perror("recv"); return false; }
        if (ch == '\n') break;
        line.push_back(ch);
        if (line.size() > 8192) { line.clear(); return false; }
    }
    return true;
}

// Engine loop: pop, match (FIFO), then send resulting lines back to the client.
static void engine_loop(OrderQueue& q, std::atomic<bool>& running, int64_t tick_factor) {
    OrderBook book;

    auto fmt_price = [tick_factor](int64_t ticks) -> std::string {
        std::ostringstream oss;
        oss.setf(std::ios::fixed); oss.precision(2);
        oss << (static_cast<double>(ticks) / static_cast<double>(tick_factor));
        return oss.str();
    };

    while (running) {
        auto mo = q.pop();
        if (!mo.has_value()) break; // stopping and drained
        const OrderMsg& m = *mo;

        auto lines = book.processOrder(m.side, m.qty, m.price_ticks, m.order_id, fmt_price);
        if (lines.empty()) continue;

        std::ostringstream out;
        for (auto& l : lines) out << l << "\n";
        const std::string payload = out.str();
        (void)safe_send(m.client_fd, payload.c_str(), payload.size());
    }
}

static void serve_client(int client_fd, OrderQueue& q, int64_t tick_factor) {
    std::string line;
    while (g_running) {
        if (!read_line(client_fd, line)) { std::cout << "Client disconnected.\n"; break; }
        if (line.empty()) { std::cout << "Empty line -> close.\n"; break; }

        // QUIT: reply and close client
        if (line == "QUIT") {
            auto now = std::chrono::high_resolution_clock::now();
            long long ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
            std::ostringstream bye; bye << "ACK " << ts_us << "\nBYE\n";
            std::string payload = bye.str();
            (void)safe_send(client_fd, payload.c_str(), payload.size());
            std::cout << "Client requested QUIT.\n";
            break;
        }

        // Parse: NEW BUY|SELL <qty> @ <price>
        std::istringstream iss(line);
        std::string type, sideStr; int qty = 0; char at = 0; double price = 0.0;
        iss >> type >> sideStr >> qty >> at >> price;

        // ACK first for latency measurement
        auto now = std::chrono::high_resolution_clock::now();
        long long ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        {
            std::ostringstream wire; wire << "ACK " << ts_us << "\n";
            (void)safe_send(client_fd, wire.str().c_str(), wire.str().size());
        }

        // Validate
        if (type != "NEW" || (sideStr != "BUY" && sideStr != "SELL") || at != '@' || qty <= 0 || price <= 0.0) {
            const char* err = "ERROR Invalid order. Expected: NEW BUY|SELL <qty> @ <price>\n";
            (void)safe_send(client_fd, err, std::strlen(err));
            continue;
        }

        // Convert float price -> integer ticks
        int64_t price_ticks = static_cast<int64_t>(std::llround(price * static_cast<double>(tick_factor)));
        if (price_ticks <= 0) {
            const char* err = "ERROR Invalid price ticks\n";
            (void)safe_send(client_fd, err, std::strlen(err));
            continue;
        }

        // Enqueue
        OrderMsg msg;
        msg.side        = (sideStr == "BUY" ? Side::Buy : Side::Sell);
        msg.qty         = qty;
        msg.price_ticks = price_ticks;
        msg.order_id    = g_order_id.fetch_add(1, std::memory_order_relaxed);
        msg.client_fd   = client_fd;

        if (!q.push(msg)) {
            const char* err = "ERROR Engine offline\n";
            (void)safe_send(client_fd, err, std::strlen(err));
            break;
        }
    }

    close(client_fd);
}

int main() {
    std::signal(SIGINT,  handle_sigint);
    std::signal(SIGPIPE, SIG_IGN);

    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Tick factor: 1 tick = £0.01 (two decimal places). Adjust if you want finer ticks.
    const int64_t TICK_FACTOR = 100;

    // 1) Socket
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) { perror("socket"); return 1; }
    int yes = 1; setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // 2) Bind / listen
    sockaddr_in addr{}; addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);
    if (bind(g_server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(g_server_fd); return 1; }
    if (listen(g_server_fd, 64) < 0) { perror("listen"); close(g_server_fd); return 1; }

    std::cout << "Exchange waiting for connections... (Ctrl-C to quit)\n";

    // 3) Start engine thread
    OrderQueue queue(4096);
    std::atomic<bool> engine_running{true};
    std::thread engine_thr(engine_loop, std::ref(queue), std::ref(engine_running), TICK_FACTOR);

    // 4) Accept loop – thread-per-connection
    while (g_running) {
        socklen_t len = sizeof(addr);
        int client_fd = accept(g_server_fd, (sockaddr*)&addr, &len);
        if (client_fd < 0) { if (!g_running) break; perror("accept"); continue; }
        std::cout << "Client connected!\n";
        std::thread(serve_client, client_fd, std::ref(queue), TICK_FACTOR).detach();
    }

    // 5) Shutdown
    if (g_server_fd >= 0) close(g_server_fd);
    engine_running = false;
    queue.stop();
    if (engine_thr.joinable()) engine_thr.join();

    std::cout << "Server shut down.\n";
    return 0;
}