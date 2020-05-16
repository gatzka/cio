cmake_minimum_required(VERSION 3.9)
include(defaults.cmake)

configure_file(cio_os_config.h.in ${PROJECT_BINARY_DIR}/generated/cio_os_config.h)

add_library(PLATFORM OBJECT
    ../shared/cio_inet_address_impl.c
    ../shared/cio_socket_address_impl.c
    cio_error_code_impl.h
    cio_eventloop_impl.h
    cio_inet4_socket_address_impl.h
    cio_inet6_socket_address_impl.h
    cio_inet_address_impl.h
    cio_linux_endian.c
    cio_linux_epoll.c
    cio_linux_random.c
    cio_linux_server_socket.c
    cio_linux_socket.c
    cio_linux_socket.h
    cio_linux_socket_utils.c
    cio_linux_socket_utils.h
    cio_linux_string.c
    cio_linux_timer.c
    cio_linux_uart.c
    cio_os_config.h.in
    cio_server_socket_impl.h
    cio_address_family_impl.h
    cio_socket_address_impl.h
    cio_socket_impl.h
    cio_timer_impl.h
    cio_unix_address.c
    cio_unix_address.h
)

set_property(TARGET PLATFORM PROPERTY PUBLIC_HEADER
    "linux/cio_address_family_impl.h"
    "linux/cio_error_code_impl.h"
    "linux/cio_eventloop_impl.h"
    "linux/cio_inet4_socket_address_impl.h"
    "linux/cio_inet6_socket_address_impl.h"
    "linux/cio_inet_address_impl.h"
    "linux/cio_server_socket_impl.h"
    "linux/cio_socket_address_impl.h"
    "linux/cio_socket_impl.h"
    "linux/cio_timer_impl.h"
)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/cio_unix_address.h"
    DESTINATION include/cio/linux/
)

target_include_directories(PLATFORM
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../../generated/ ${CMAKE_CURRENT_LIST_DIR}/../../
)

get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    if(CIO_ENABLE_LTO)
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endforeach()
