#pragma once
#include <atomic>
#include <string>
#include <cstdint>
#include <vector>

class Bot {
public:
    Bot(std::string host, uint16_t port, int clients, int ordersPerClient);

    // Average RTT only (existing behaviour)
    long long run();

    // NEW: collect all RTT samples (microseconds)
    std::vector<long long> run_collect();

private:
    void worker(int id, std::atomic<long long>& rttSum);
    void worker_collect(int id, std::vector<long long>& outSamples);

    int connect_once() const;

    std::string host_;
    uint16_t port_;
    int clients_;
    int ordersPerClient_;
};