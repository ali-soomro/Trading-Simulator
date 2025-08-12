find_package(Threads REQUIRED)

add_library(orderbook STATIC order_book.cpp)
target_include_directories(orderbook PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(enginequeue STATIC engine_queue.cpp)
target_include_directories(enginequeue PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_executable(exchange exchange.cpp)
target_include_directories(exchange PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(exchange PRIVATE orderbook enginequeue Threads::Threads)

add_executable(client client.cpp)
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR}/include)

# If your bot has main() in src/bot.cpp, keep this:
add_executable(bot bot.cpp)
target_include_directories(bot PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(bot PRIVATE Threads::Threads)