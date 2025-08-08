#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <netinet/tcp.h>   // TCP_NODELAY
#include <sys/time.h>      // timeval for SO_RCVTIMEO

static void set_recv_timeout(int sock, int millis) {
    timeval tv{}; tv.tv_sec = millis/1000; tv.tv_usec = (millis%1000)*1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

static int read_line(int fd, std::string& line) {
    line.clear();
    char ch = 0;
    while (true) {
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n == 0)  return -1; // EOF
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; // timeout
            perror("recv"); return -1;
        }
        if (ch == '\n') break;
        line.push_back(ch);
        if (line.size() > 8192) { std::cerr << "Line too long\n"; return -1; }
    }
    return 1;
}

static long long parse_ack_ts(const std::string& s) {
    const std::string tag = "ACK ";
    auto p = s.find(tag); if (p == std::string::npos) return -1;
    p += tag.size();
    size_t q = p; while (q < s.size() && isdigit(static_cast<unsigned char>(s[q]))) ++q;
    try { return std::stoll(s.substr(p, q - p)); } catch (...) { return -1; }
}

int main() {
    // Socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }
    int one = 1; setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    // Connect
    sockaddr_in serv{}; serv.sin_family = AF_INET; serv.sin_port = htons(8080);
    if (inet_pton(AF_INET, "127.0.0.1", &serv.sin_addr) <= 0) { std::cerr << "Invalid address\n"; return 1; }
    if (connect(sock, (sockaddr*)&serv, sizeof(serv)) < 0) { perror("connect"); return 1; }

    set_recv_timeout(sock, 100); // to drain trailing lines without blocking forever

    std::cout << "Connected. Type orders like:\n";
    std::cout << "  NEW BUY 100 @ 50.25\n  NEW SELL 60 @ 50.10\n  QUIT\n\n";

    std::string user;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, user)) break;     // EOF on stdin
        if (user.empty()) continue;
        user.push_back('\n');

        auto t_send = std::chrono::high_resolution_clock::now();
        long long send_us = std::chrono::duration_cast<std::chrono::microseconds>(t_send.time_since_epoch()).count();

        if (send(sock, user.c_str(), user.size(), 0) < 0) { perror("send"); break; }

        // Wait for first line (ACK …)
        std::string line;
        while (true) {
            int rc = read_line(sock, line);
            if (rc == -1) { std::cout << "Server closed.\n"; close(sock); return 0; }
            if (rc ==  1) break; // got a line
            // rc == 0 -> timeout, keep waiting for ACK
        }
        auto t_recv = std::chrono::high_resolution_clock::now();
        long long recv_us = std::chrono::duration_cast<std::chrono::microseconds>(t_recv.time_since_epoch()).count();

        std::cout << line << "\n"; // should be ACK <ts>
        long long svr_us = parse_ack_ts(line);
        if (svr_us >= 0) {
            std::cout << "RTT: " << (recv_us - send_us) << " us, approx one-way: " << (svr_us - send_us) << " us\n";
        }

        // Drain any extra lines from server for this order
        while (true) {
            int rc = read_line(sock, line);
            if (rc == 1) {
                if (!line.empty()) std::cout << line << "\n";
                if (line == "BYE") { close(sock); return 0; }
            } else if (rc == 0) {
                break; // timeout -> assume no more lines for this order
            } else {
                std::cout << "Server closed.\n"; close(sock); return 0;
            }
        }
    }

    close(sock);
    return 0;
}