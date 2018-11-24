cmake_minimum_required(VERSION 3.7)
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

get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    target_compile_options(${tgt} PRIVATE $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wall;-Wextra;-Wshadow;-Winit-self;-Wcast-qual;-Wcast-align;-Wformat=2;-Wwrite-strings;-Wmissing-prototypes;-Wstrict-prototypes;-Wold-style-definition;-Wstrict-overflow=5;-Wdisabled-optimization;-Wmissing-include-dirs;-Wswitch-default;-Wundef;-Wconversion;-pedantic;-pedantic-errors>)
    target_compile_options(${tgt} PRIVATE $<$<AND:$<C_COMPILER_ID:GNU>,$<VERSION_GREATER:$<C_COMPILER_VERSION>,7.0.0>>:-Wformat-truncation -Wstringop-overflow -Wformat-overflow=2>)
    target_compile_options(${tgt} PRIVATE $<$<AND:$<C_COMPILER_ID:GNU>,$<VERSION_GREATER:$<C_COMPILER_VERSION>,8.0.0>>:-Wstringop-truncation>)
    
    target_compile_options(${tgt} PRIVATE $<$<C_COMPILER_ID:Clang>:-Wdocumentation>)
endforeach()
