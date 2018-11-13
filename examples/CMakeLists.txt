cmake_minimum_required(VERSION 3.0)
project(cio-examples C)

add_executable(socket_echo
    socket_echo.c
)
target_link_libraries (socket_echo cio:cio)

add_executable(http_server
    http_server.c
)
target_link_libraries (http_server cio:cio)

add_executable(websocket_server
    websocket_server.c
)
target_link_libraries (websocket_server cio:cio)

add_executable(periodic_timer
    periodic_timer.c
)
target_link_libraries (periodic_timer cio:cio)

