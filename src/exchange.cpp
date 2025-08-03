#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <chrono>

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) { perror("Socket failed"); exit(EXIT_FAILURE); }

    // Set up address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    // Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed"); exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed"); exit(EXIT_FAILURE);
    }
    std::cout << "Exchange waiting for connection...\n";

    // Accept
    new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (new_socket < 0) { perror("Accept failed"); exit(EXIT_FAILURE); }
    std::cout << "Client connected!\n";

    // Read order
    read(new_socket, buffer, 1024);
    std::cout << "Received: " << buffer << std::endl;

    // Server timestamp
    auto t_server_recv = std::chrono::high_resolution_clock::now();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                      t_server_recv.time_since_epoch()).count();

    // Send ACK with timestamp
    std::string ack = "ACK " + std::to_string(micros) + "\n";
    send(new_socket, ack.c_str(), ack.size(), 0);

    // Close
    close(new_socket);
    close(server_fd);
    return 0;
}