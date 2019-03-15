cmake_minimum_required(VERSION 3.7)
project(cio-examples C)

add_executable(socket_echo
    socket_echo.c
)
target_link_libraries (socket_echo)

add_executable(http_server
    http_server.c
)
target_link_libraries (http_server)

add_executable(websocket_server
    websocket_server.c
)
target_link_libraries (websocket_server)

add_executable(periodic_timer
    periodic_timer.c
)
target_link_libraries (periodic_timer)

get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    get_target_property(target_type ${tgt} TYPE)
    if (target_type STREQUAL "EXECUTABLE")
        target_link_libraries(${tgt} cio:cio)
        set_target_properties(${tgt} PROPERTIES
    		C_STANDARD_REQUIRED ON
    		C_EXTENSIONS OFF
            C_CLANG_TIDY ""
        )
    endif()
endforeach()
