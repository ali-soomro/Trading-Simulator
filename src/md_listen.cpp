#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    std::string host = "0.0.0.0"; // bind all
    uint16_t port = 9001;
    if (argc >= 2) port = static_cast<uint16_t>(std::atoi(argv[1]));

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{}; addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host.c_str());

    if (bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(s); return 1;
    }

    std::cout << "UDP MD listener on " << host << ":" << port << "\n";
    char buf[2048];
    while (true) {
        ssize_t n = recvfrom(s, buf, sizeof(buf)-1, 0, nullptr, nullptr);
        if (n <= 0) break;
        buf[n] = '\0';
        std::cout << buf << std::endl;
    }
    close(s);
    return 0;
}