#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <chrono>

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation error\n"; return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address\n"; return -1;
    }

    // Connect
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection Failed\n"; return -1;
    }

    std::string order = "NEW BUY 100 @ 50.25\n";

    // Timestamp before sending
    auto t_client_send = std::chrono::high_resolution_clock::now();
    auto send_micros = std::chrono::duration_cast<std::chrono::microseconds>(
                           t_client_send.time_since_epoch()).count();

    // Send order
    send(sock, order.c_str(), order.size(), 0);
    std::cout << "Order sent!\n";

    // Receive ACK
    read(sock, buffer, 1024);
    auto t_client_recv = std::chrono::high_resolution_clock::now();
    auto recv_micros = std::chrono::duration_cast<std::chrono::microseconds>(
                           t_client_recv.time_since_epoch()).count();

    std::cout << "Server: " << buffer;

    // Parse server timestamp
    long long server_micros;
    sscanf(buffer, "ACK %lld", &server_micros);

    // Compute latencies
    long long rtt = recv_micros - send_micros;
    long long one_way = server_micros - send_micros;

    std::cout << "Round-trip latency: " << rtt << " microseconds\n";
    std::cout << "Approx. one-way latency (client->server): "
              << one_way << " microseconds\n";

    close(sock);
    return 0;
}