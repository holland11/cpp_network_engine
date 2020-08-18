cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(cpp_network_engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

set(Boost_USE_MULTITHREADED ON)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -lncurses")

find_package(Boost 1.72.0 REQUIRED COMPONENTS timer system thread)

add_library(cpp_network lib/net_message.cpp lib/net_client.cpp lib/net_server.cpp)
target_include_directories(cpp_network PUBLIC ${Boost_INCLUDE_DIRS} include)
target_link_libraries(cpp_network LINK_PUBLIC ${Boost_LIBRARIES})

add_executable(chat_server app/chat_server.cpp app/chat_constants.hpp)
target_link_libraries(chat_server cpp_network ncurses)
add_executable(chat_client app/chat_client.cpp app/chat_constants.hpp)
target_link_libraries(chat_client cpp_network ncurses)

add_executable(connect4_server app/connect4_server.cpp)
target_link_libraries(connect4_server cpp_network ncurses)
add_executable(connect4_client app/connect4_client.cpp)
target_link_libraries(connect4_client cpp_network ncurses)