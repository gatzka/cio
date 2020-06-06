cmake_minimum_required(VERSION 3.9)

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
    cio_windows_endian.c
    cio_windows_iocp.c
    cio_windows_random.c
    cio_windows_server_socket.c
    cio_windows_socket.c
    cio_windows_socket.h
    cio_windows_socket_utils.c
    cio_windows_socket_utils.h
    cio_windows_string.c
    cio_windows_timer.c
)

set_property(TARGET PLATFORM 
    PROPERTY PUBLIC_HEADER
        "platform/windows/cio_error_code_impl.h"
        "platform/windows/cio_eventloop_impl.h"
        "platform/windows/cio_server_socket_impl.h"
        "platform/windows/cio_socket_impl.h"
        "platform/windows/cio_timer_impl.h"
)

target_include_directories(PLATFORM
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../../generated/ ${CMAKE_CURRENT_LIST_DIR}/../../
)

target_link_libraries(PLATFORM bcrypt ws2_32)

get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    if(CIO_ENABLE_LTO)
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endforeach()

