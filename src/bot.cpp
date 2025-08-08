#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <netinet/tcp.h>

static int connect_once(const char* host, uint16_t port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return -1; }
#ifdef TCP_NODELAY
    int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &a.sin_addr) <= 0) { std::cerr << "bad host\n"; close(s); return -1; }
    if (connect(s, (sockaddr*)&a, sizeof(a)) < 0) { perror("connect"); close(s); return -1; }
    return s;
}

static bool read_line(int fd, std::string& out) {
    out.clear(); char ch=0;
    while (true) {
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n == 0) return false;
        if (n < 0) { perror("recv"); return false; }
        if (ch == '\n') break;
        out.push_back(ch);
        if (out.size() > 8192) return false;
    }
    return true;
}

static void worker(int id, int orders, const char* host, uint16_t port, std::atomic<long long>& rtt_sum) {
    int s = connect_once(host, port);
    if (s < 0) return;

    std::mt19937_64 rng(id * 1337ULL);
    std::uniform_int_distribution<int> side_dist(0,1);
    std::uniform_int_distribution<int> qty_dist(1, 200);
    std::uniform_int_distribution<int> pips_dist(-20, 20); // around 50.25

    for (int i=0; i<orders; ++i) {
        double px = 50.25 + pips_dist(rng) * 0.01;
        int qty = qty_dist(rng);
        std::string side = side_dist(rng) ? "BUY" : "SELL";
        std::string line = "NEW " + side + " " + std::to_string(qty) + " @ " + std::to_string(px) + "\n";

        auto t0 = std::chrono::high_resolution_clock::now();
        if (send(s, line.c_str(), line.size(), 0) < 0) { perror("send"); break; }

        // Expect first line = ACK ...
        std::string resp;
        if (!read_line(s, resp)) break;
        auto t1 = std::chrono::high_resolution_clock::now();
        auto rtt = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        rtt_sum += rtt;

        // Drain extra lines for this order (TRADE/ORDER_ADDED/BEST_*)
        // Non-blocking-ish: small timeout via SO_RCVTIMEO could be added, but we’ll just peek
        suseconds_t(1000); // tiny pause to let server flush
        std::string tmp;
        while (true) {
            struct timeval tv{0, 1000}; // 1ms
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            int rc = recv(s, &tmp, 0, MSG_PEEK);
            if (rc <= 0) break;
            if (!read_line(s, tmp)) break;
            if (tmp.empty()) break;
            // (optional) std::cout << "C" << id << ": " << tmp << "\n";
        }
    }

    // quit politely
    const char* bye = "QUIT\n"; (void)send(s, bye, strlen(bye), 0);
    close(s);
}

int main(int argc, char** argv) {
    const char* host = "127.0.0.1";
    uint16_t port = 8080;
    int clients = 4;
    int orders_per_client = 200;

    if (argc >= 2) clients = std::atoi(argv[1]);
    if (argc >= 3) orders_per_client = std::atoi(argv[2]);

    std::cout << "Bot: " << clients << " clients x " << orders_per_client << " orders\n";

    std::vector<std::thread> ts;
    std::atomic<long long> rtt_sum{0};

    for (int i=0; i<clients; ++i) ts.emplace_back(worker, i, orders_per_client, host, port, std::ref(rtt_sum));
    for (auto& t : ts) t.join();

    long long total = 1LL * clients * orders_per_client;
    std::cout << "Avg RTT ≈ " << (rtt_sum.load() / std::max(1LL,total)) << " us\n";
    return 0;
}