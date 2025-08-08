#include "bot.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <unistd.h>
#include <netinet/tcp.h>
#include <vector>
#include <sys/time.h>
#include <cerrno>
#include <cstdlib>

// --- NEW: safe_send helper ---
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

Bot::Bot(std::string host, uint16_t port, int clients, int ordersPerClient)
    : host_(std::move(host)),
      port_(port),
      clients_(clients),
      ordersPerClient_(ordersPerClient) {}

int Bot::connect_once() const {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return -1; }
#ifdef TCP_NODELAY
    int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &a.sin_addr) <= 0) {
        std::cerr << "bad host\n";
        close(s);
        return -1;
    }
    if (connect(s, (sockaddr*)&a, sizeof(a)) < 0) {
        perror("connect");
        close(s);
        return -1;
    }
    return s;
}

// --- UPDATED: allow non-fatal timeouts (EAGAIN) when draining ---
static bool read_line_fd(int fd, std::string& out, bool nonfatal_timeout) {
    out.clear(); char ch=0;
    while (true) {
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n == 0) return false; // peer closed
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return nonfatal_timeout ? false : false; // caller decides; false means "no line available"
            }
            perror("recv");
            return false;
        }
        if (ch == '\n') break;
        out.push_back(ch);
        if (out.size() > 8192) return false;
    }
    return true;
}

void Bot::worker(int id, std::atomic<long long>& rttSum) {
    int s = connect_once();
    if (s < 0) return;

    std::mt19937_64 rng(id * 1337ULL);
    std::uniform_int_distribution<int> side_dist(0,1);
    std::uniform_int_distribution<int> qty_dist(1, 200);
    std::uniform_int_distribution<int> pips_dist(-20, 20); // around 50.25

    for (int i=0; i<ordersPerClient_; ++i) {
        double px = 50.25 + pips_dist(rng) * 0.01;
        int qty = qty_dist(rng);
        std::string side = side_dist(rng) ? "BUY" : "SELL";
        std::string line = "NEW " + side + " " + std::to_string(qty) + " @ " + std::to_string(px) + "\n";

        auto t0 = std::chrono::high_resolution_clock::now();
        if (safe_send(s, line.c_str(), line.size()) < 0) { /*perror("send");*/ break; }

        // Expect first line = ACK ...
        std::string resp;
        while (true) {
            if (!read_line_fd(s, resp, /*nonfatal_timeout=*/false)) {
                // false here means either EOF or (unexpected) error; bail this client
                return;
            }
            if (!resp.empty()) break; // got a line (ACK)
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        auto rtt = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        rttSum += rtt;

        // Drain extra lines with a small timeout; silence EAGAIN.
        struct timeval tv{0, 2000}; // 2ms
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        std::string tmp;
        while (true) {
            if (!read_line_fd(s, tmp, /*nonfatal_timeout=*/true)) {
                // No full line available within timeout -> stop draining quietly
                break;
            }
            if (tmp.empty()) break;
            // optional: std::cout << "extra: " << tmp << "\n";
        }
    }

    // polite quit
    const char* bye = "QUIT\n";
    (void)safe_send(s, bye, strlen(bye));
    close(s);
}

long long Bot::run() {
    std::vector<std::thread> ts;
    std::atomic<long long> rttSum{0};

    for (int i=0; i<clients_; ++i)
        ts.emplace_back(&Bot::worker, this, i, std::ref(rttSum));
    for (auto& t : ts) t.join();

    long long totalOrders = 1LL * clients_ * ordersPerClient_;
    return (totalOrders > 0) ? (rttSum.load() / totalOrders) : 0;
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    int clients = 4;
    int orders = 200;

    if (argc >= 2) clients = std::atoi(argv[1]);
    if (argc >= 3) orders = std::atoi(argv[2]);

    Bot bot(host, port, clients, orders);
    long long avg = bot.run();

    std::cout << "Bot finished. Avg RTT ≈ " << avg << " us\n";
    return 0;
}