# Low-Latency Trading Simulator

A high-performance matching engine and market data simulator built in modern C++ for **low-latency trading systems research**.  
Designed to replicate core components of exchange infrastructure, with a focus on **microsecond-level performance**, **concurrency**, and **deterministic order matching**.

---

## 🚀 Features

- **Multi-client TCP matching engine** with price–time priority  
  Sustains **>50,000 orders/sec** with **<200 μs** average round-trip latency under realistic load.

- **UDP market-data publisher**  
  Broadcasts order-book snapshots and trade ticks at **sub-100 μs** one-way latency, enabling direct TCP vs UDP feed latency comparison.

- **Fully unit-tested order book engine** (GoogleTest)  
  >95% coverage, supports marketable/resting orders, full order matching, and continuous best-bid/ask snapshots.

- **Multi-threaded load-testing bot**  
  Simulates thousands of orders/sec from concurrent client connections to benchmark exchange throughput.

- **Lock-efficient & modular architecture**  
  Built for scalability with minimal contention, reproducible microsecond-precision benchmarks, and a modular CMake build system.

---

## 📦 Components

| Component        | Description |
|------------------|-------------|
| `exchange`       | TCP server matching engine, manages order books, and publishes market data via UDP. |
| `client`         | CLI client for manual order submission and latency measurement. |
| `bot`            | Multi-threaded load generator for stress testing and benchmarking. |
| `md_listen`      | UDP market-data listener for real-time feed monitoring. |
| `order_book`     | Core matching engine logic (price-time priority, order management). |
| `tests`          | GoogleTest unit tests for deterministic order book behaviour. |

---

## 📊 Performance

**Test environment**:  
- C++20, `-O3` optimisation  
- TCP_NODELAY enabled  
- Local loopback on macOS/Linux  

| Scenario | Orders/sec | Avg RTT (μs) | One-way Latency UDP (μs) |
|----------|------------|--------------|--------------------------|
| 4 clients × 200 orders | 50,000+ | <200 | <100 |

---

## 🛠 Build & Run

```bash
# Build with CMake from the root of the repository
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run exchange
./src/exchange

# In another terminal, run a client
./src/client

# Or run the load-testing bot
./src/bot 4 200

# Listen to market data
./src/md_listen
