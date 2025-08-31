#include "bot.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>
#include <unistd.h>
#include <netinet/tcp.h>
#include <vector>
#include <sys/time.h>
#include <cerrno>
#include <cstdlib>
#include <algorithm>
#include <atomic>

// --- TCP safe send ---
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

// --- UDP publish (for RTT → frontend) ---
static void send_udp(const std::string& msg, const std::string& host, uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    sendto(sock, msg.c_str(), msg.size(), 0, (sockaddr*)&addr, sizeof(addr));
    close(sock);
}

// --- Utility: read one line ---
static bool read_line_fd(int fd, std::string& out, bool nonfatal_timeout) {
    out.clear(); char ch=0;
    while (true) {
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n == 0) return false; // peer closed
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
            perror("recv"); return false;
        }
        if (ch == '\n') break;
        out.push_back(ch);
        if (out.size() > 8192) return false;
    }
    return true;
}

// --- Percentile helper ---
static long long percentile(std::vector<long long>& v, double p) {
    if (v.empty()) return 0;
    size_t idx = static_cast<size_t>(p * (v.size()-1));
    std::nth_element(v.begin(), v.begin()+idx, v.end());
    return v[idx];
}

// Send one line and wait for a single '\n'-terminated reply (ACK or first line)
static bool send_and_wait_ack(int s, const std::string& line) {
    if (send(s, line.c_str(), line.size(), 0) < 0) return false;
    std::string resp; resp.reserve(256);
    char ch;
    while (true) {
        ssize_t n = recv(s, &ch, 1, 0);
        if (n <= 0) return false;
        if (ch == '\n') break;
        resp.push_back(ch);
        if (resp.size() > 4096) break;
    }
    return true;
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    int clients = 4;
    int orders  = 200;
    std::string csvPath;

    // NEW demo flags
    bool demoBuy  = false; // seed asks then lift them
    bool demoSell = false; // seed bids then hit them

    // Args: [clients] [orders] [--csv file] [--demo-buy] [--demo-sell]
    for (int i=1; i<argc; ++i) {
        std::string a = argv[i];
        if (a == "--csv" && i+1 < argc) csvPath = argv[++i];
        else if (a == "--demo-buy")  demoBuy  = true;
        else if (a == "--demo-sell") demoSell = true;
        else if (i == 1 && a.rfind("--",0) != 0) { clients = std::atoi(argv[i]); }
        else if (i == 2 && a.rfind("--",0) != 0) { orders  = std::atoi(argv[i]); }
    }

    // --- OPTIONAL DEMO PRELUDE ---
    if (demoBuy || demoSell) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s >= 0) {
#ifdef TCP_NODELAY
            int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
            sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
            inet_pton(AF_INET, host.c_str(), &a.sin_addr);
            if (connect(s, (sockaddr*)&a, sizeof(a)) == 0) {
                // BUY demo: create resting asks, then send BUY that crosses
                if (demoBuy) {
                    send_and_wait_ack(s, "NEW SELL 200 @ 50.30\n");
                    send_and_wait_ack(s, "NEW SELL 200 @ 50.28\n");
                    send_and_wait_ack(s, "NEW BUY  350 @ 50.35\n");   // ⇒ TRADE BUY ...
                }
                // SELL demo: create resting bids, then send SELL that crosses
                if (demoSell) {
                    send_and_wait_ack(s, "NEW BUY  200 @ 50.20\n");
                    send_and_wait_ack(s, "NEW BUY  200 @ 50.18\n");
                    send_and_wait_ack(s, "NEW SELL 350 @ 50.15\n");   // ⇒ TRADE SELL ...
                }
                const char* bye = "QUIT\n"; (void)send(s, bye, strlen(bye), 0);
            }
            close(s);
        }
        return 0; // exit after demo; remove this 'return' if you want to run load test too
    }

    // --- NORMAL LOAD TEST BELOW (unchanged) ---
    std::vector<std::thread> ts;
    std::vector<std::vector<long long>> perThread(clients);

    auto worker_collect = [&](int id){
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return;
#ifdef TCP_NODELAY
        int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &a.sin_addr);
        if (connect(s, (sockaddr*)&a, sizeof(a)) < 0) { close(s); return; }

        std::mt19937_64 rng(id * 1337ULL);
        std::uniform_int_distribution<int> side_dist(0,1);
        std::uniform_int_distribution<int> qty_dist(1, 200);
        std::uniform_int_distribution<int> pips_dist(-20, 20);
        perThread[id].reserve(orders);

        for (int i=0; i<orders; ++i) {
            double px = 50.25 + pips_dist(rng) * 0.01;
            int qty = qty_dist(rng);
            std::string side = side_dist(rng) ? "BUY" : "SELL";
            std::string line = "NEW " + side + " " + std::to_string(qty) + " @ " + std::to_string(px) + "\n";

            auto t0 = std::chrono::high_resolution_clock::now();
            if (send(s, line.c_str(), line.size(), 0) < 0) break;

            std::string resp;
            if (!read_line_fd(s, resp, false)) { close(s); return; }
            auto t1 = std::chrono::high_resolution_clock::now();
            long long rtt = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            perThread[id].push_back(rtt);

            // Publish RTT over UDP (for frontend graph)
            send_udp("RTT " + std::to_string(rtt) + "\n", "127.0.0.1", 9001);

            struct timeval tv{0, 2000}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            std::string tmp; while (true) { if (!read_line_fd(s, tmp, true)) break; if (tmp.empty()) break; }
        }
        const char* bye = "QUIT\n"; (void)send(s, bye, strlen(bye), 0); close(s);
    };

    for (int i=0; i<clients; ++i) ts.emplace_back(worker_collect, i);
    for (auto& t : ts) t.join();

    // Merge all RTT samples
    std::vector<long long> samples;
    size_t total = 0; for (auto& v : perThread) total += v.size();
    samples.reserve(total);
    for (auto& v : perThread) { samples.insert(samples.end(), v.begin(), v.end()); }

    if (samples.empty()) { std::cout << "No samples collected.\n"; return 1; }

    // Percentiles
    auto p50 = percentile(samples, 0.50);
    auto p95 = percentile(samples, 0.95);
    auto p99 = percentile(samples, 0.99);
    auto pmax = *std::max_element(samples.begin(), samples.end());

    std::cout << "Samples: " << samples.size() << "\n"
              << "p50: " << p50 << " us\n"
              << "p95: " << p95 << " us\n"
              << "p99: " << p99 << " us\n"
              << "max: " << pmax << " us\n";

    if (!csvPath.empty()) {
        std::ofstream csv(csvPath);
        csv << "percentile,value_us\n";
        csv << "p50," << p50 << "\n";
        csv << "p95," << p95 << "\n";
        csv << "p99," << p99 << "\n";
        csv << "max," << pmax << "\n";
        csv.close();
        std::cout << "Wrote " << csvPath << "\n";
    }
    return 0;
}