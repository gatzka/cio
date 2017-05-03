cmake_minimum_required(VERSION 2.8)
project(cio-tests C)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

add_library(unity
    ../../unity/src/unity.c
    ../../unity/src/unity.h
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../fff/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../unity/src/)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")

add_executable(test_cio_linux_epoll
    test_cio_linux_epoll.c
    ../cio_linux_epoll.c
)
target_link_libraries (test_cio_linux_epoll unity)

add_executable(test_cio_linux_server_socket
    test_cio_linux_server_socket.c
    ../cio_linux_server_socket.c
)
target_link_libraries (test_cio_linux_server_socket unity)

enable_testing()
add_test(NAME test_cio_linux_server_socket COMMAND test_cio_linux_server_socket)
add_test(NAME test_cio_linux_epoll COMMAND test_cio_linux_epoll)

