cmake_minimum_required(VERSION 3.8)

set(CIO_PLATFORM_SOURCES
    windows/cio_windows_iocp.c
    windows/cio_windows_endian.c
    windows/cio_windows_random.c
    windows/cio_windows_server_socket.c
    windows/cio_windows_socket.c
    windows/cio_windows_string.c
    windows/cio_windows_timer.c
)

target_sources(${PROJECT_NAME} PRIVATE 
    ${CIO_PLATFORM_SOURCES}
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
)

set_property(TARGET ${PROJECT_NAME}
    APPEND
    PROPERTY PUBLIC_HEADER "windows/cio_error_code_impl.h;windows/cio_eventloop_impl.h;windows/cio_server_socket_impl.h;windows/cio_socket_impl.h;windows/cio_windows_socket.h"
)

target_link_libraries(${PROJECT_NAME} bcrypt ws2_32)
