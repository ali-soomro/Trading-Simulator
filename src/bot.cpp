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
    : host_(std::move(host)), port_(port), clients_(clients), ordersPerClient_(ordersPerClient) {}

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
        if (safe_send(s, line.c_str(), line.size()) < 0) break;

        std::string resp;
        while (true) {
            if (!read_line_fd(s, resp, /*nonfatal_timeout=*/false)) { close(s); return; }
            if (!resp.empty()) break;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        auto rtt = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        rttSum += rtt;

        struct timeval tv{0, 2000}; // 2ms
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::string tmp;
        while (true) {
            if (!read_line_fd(s, tmp, /*nonfatal_timeout=*/true)) break;
            if (tmp.empty()) break;
        }
    }

    const char* bye = "QUIT\n";
    (void)safe_send(s, bye, strlen(bye));
    close(s);
}

static long long percentile(std::vector<long long>& v, double p) {
    if (v.empty()) return 0;
    size_t idx = static_cast<size_t>(p * (v.size()-1));
    std::nth_element(v.begin(), v.begin()+idx, v.end());
    return v[idx];
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    int clients = 4;
    int orders = 200;
    std::string csvPath;

    // Flags: --csv <path>
    for (int i=1; i<argc; ++i) {
        std::string a = argv[i];
        if (a == "--csv" && i+1 < argc) csvPath = argv[++i];
        else if (i == 1 && a.find("--") != 0) { clients = std::atoi(argv[i]); }
        else if (i == 2 && a.find("--") != 0) { orders  = std::atoi(argv[i]); }
    }

    // Collect RTTs by running the same logic but capturing samples
    std::vector<std::thread> ts;
    std::vector<std::vector<long long>> perThread(clients);
    std::atomic<long long> dummy{0};

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
            perThread[id].push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

            struct timeval tv{0, 2000}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            std::string tmp; while (true) { if (!read_line_fd(s, tmp, true)) break; if (tmp.empty()) break; }
        }
        const char* bye = "QUIT\n"; (void)send(s, bye, strlen(bye), 0); close(s);
    };

    for (int i=0; i<clients; ++i) ts.emplace_back(worker_collect, i);
    for (auto& t : ts) t.join();

    // Merge
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