cmake_minimum_required(VERSION 3.0)
project(cio-tests C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../../../cmake/)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

include(sanitizer)

add_library(unity
    ../../unity/src/unity.c
    ../../unity/src/unity.h
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../fff/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../unity/src/)

set(WARN_SWITCHES "-Wall -Wextra -Werror -Wshadow -Winit-self -Wcast-qual -Wcast-align -Wformat=2 -Wwrite-strings -Wmissing-prototypes -Wstrict-overflow=5 -pedantic")
set(CMAKE_C_FLAGS "-pipe -fno-common -std=gnu99 ${WARN_SWITCHES} ${CMAKE_C_FLAGS}")

message(STATUS "CMAKE_C_COMPILER_ID: " ${CMAKE_C_COMPILER_ID})
message(STATUS "CMAKE_C_COMPILER_VERSION: " ${CMAKE_C_COMPILER_VERSION})

add_executable(test_cio_linux_epoll
    test_cio_linux_epoll.c
    ../cio_linux_epoll.c
)
target_link_libraries (test_cio_linux_epoll unity)

add_executable(test_cio_linux_server_socket
    test_cio_linux_server_socket.c
    ../cio_linux_server_socket.c
    ../cio_linux_socket.c
)
target_link_libraries (test_cio_linux_server_socket unity)

enable_testing()
add_test(NAME test_cio_linux_server_socket COMMAND test_cio_linux_server_socket)
add_test(NAME test_cio_linux_epoll COMMAND test_cio_linux_epoll)

