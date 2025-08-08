add_library(orderbook STATIC order_book.cpp)
target_include_directories(orderbook PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_executable(exchange exchange.cpp)
target_link_libraries(exchange PRIVATE orderbook)

add_executable(client client.cpp)