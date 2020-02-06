cmake_minimum_required(VERSION 3.8)

add_library(PLATFORM OBJECT
    cio_windows_iocp.c
    cio_windows_endian.c
    cio_windows_random.c
    cio_windows_server_socket.c
    cio_windows_socket.c
    cio_windows_socket_utils.c
    cio_windows_string.c
    cio_windows_timer.c
)

set_property(TARGET PLATFORM 
    PROPERTY PUBLIC_HEADER "windows/cio_error_code_impl.h;windows/cio_eventloop_impl.h;windows/cio_server_socket_impl.h;windows/cio_socket_impl.h"
)

target_include_directories(PLATFORM
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../generated/ ${CMAKE_CURRENT_LIST_DIR}/../
)

target_link_libraries(PLATFORM bcrypt ws2_32)
