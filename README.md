# Low-Latency Trading Simulator

A C++ project simulating a simplified trading exchange to explore 
low-latency systems design, order book mechanics, and latency measurement.

## Features
- TCP client-server architecture
- Blocking sockets for clarity
- Round-trip latency measurement
- Approximate one-way latency measurement
- Order message format: `NEW BUY <qty> @ <price>`

## Roadmap
- [x] TCP client-server setup
- [x] Round-trip and one-way latency
- [ ] Aggregated order book
- [ ] Full order book with IDs
- [ ] Concurrency with threads
- [ ] Latency dashboard (99th percentile)

## How to Build
mkdir build
cd build
cmake ..
make