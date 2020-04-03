cmake_minimum_required(VERSION 3.8)

add_library(PLATFORM OBJECT
    cio_zephyr_endian.c
    cio_zephyr_eventloop.c
    cio_zephyr_inet_address_impl.c
    cio_zephyr_random.c
    cio_zephyr_server_socket.c
    cio_zephyr_socket.c
    cio_zephyr_socket_address_impl.c
    cio_zephyr_string.c
    cio_zephyr_timer.c
    ../platform/shared/cio_string_memmem.c
)

target_include_directories(PLATFORM
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../generated/ ${CMAKE_CURRENT_LIST_DIR}/../
)

target_link_libraries (PLATFORM PUBLIC zephyr kernel)

