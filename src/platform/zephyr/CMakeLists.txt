cmake_minimum_required(VERSION 3.8)

add_library(PLATFORM OBJECT
    ../shared/cio_inet_address_impl.c
    ../shared/cio_socket_address_impl.c
    ../shared/cio_string_memmem.c
    cio_address_family_impl.h
    cio_error_code_impl.h
    cio_eventloop_impl.h
    cio_inet4_socket_address_impl.h
    cio_inet6_socket_address_impl.h
    cio_inet_address_impl.h
    cio_server_socket_impl.h
    cio_socket_address_impl.h
    cio_socket_impl.h
    cio_timer_impl.h
    cio_zephyr_endian.c
    cio_zephyr_eventloop.c
    cio_zephyr_random.c
    cio_zephyr_server_socket.c
    cio_zephyr_socket.c
    cio_zephyr_string.c
    cio_zephyr_timer.c
)

target_include_directories(PLATFORM
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../../generated/ ${CMAKE_CURRENT_LIST_DIR}/../../
)

target_link_libraries (PLATFORM PUBLIC zephyr kernel)

