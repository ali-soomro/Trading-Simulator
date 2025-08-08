cmake_minimum_required(VERSION 3.14)
project(low_latency_trading_simulator CXX)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)