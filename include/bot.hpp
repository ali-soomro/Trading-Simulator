#pragma once

#include <atomic>
#include <string>
#include <cstdint>

// A simple load bot that connects to an exchange server and sends random orders.
class Bot {
public:
    Bot(std::string host, uint16_t port, int clients, int ordersPerClient);

    // Runs the bot: spawns all client threads, returns average RTT in microseconds
    long long run();

private:
    // Worker function for each simulated client
    void worker(int id, std::atomic<long long>& rttSum);

    // Connects once to the exchange, returns socket fd or -1 on error
    int connect_once() const;

    std::string host_;
    uint16_t port_;
    int clients_;
    int ordersPerClient_;
};