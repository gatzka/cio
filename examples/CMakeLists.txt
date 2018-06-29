cmake_minimum_required(VERSION 3.0)
project(cio-examples C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake/)
include(compiler_settings)
include(sanitizer)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src/ ${CMAKE_CURRENT_SOURCE_DIR}/../src/linux/)

add_executable(socket_echo
    socket_echo.c
)
target_link_libraries (socket_echo cio_static)

add_executable(http_server
    http_server.c
)
target_link_libraries (http_server cio_static)

add_executable(websocket_server
    websocket_server.c
)
target_link_libraries (websocket_server cio_static)

add_executable(periodic_timer
    periodic_timer.c
)
target_link_libraries (periodic_timer cio_static)

