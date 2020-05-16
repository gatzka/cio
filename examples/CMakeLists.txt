cmake_minimum_required(VERSION 3.9)
project(cio-examples C)

add_executable(socket_echo socket_echo.c)
add_executable(socket_connect socket_connect.c)
add_executable(socket_ping_pong socket_ping_pong.c)
add_executable(socket_two_connects socket_two_connects.c)
add_executable(http_server http_server.c)
add_executable(websocket_server websocket_server.c)
add_executable(periodic_timer periodic_timer.c)
add_executable(uart_ping_pong uart_ping_pong.c)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_subdirectory(linux)
endif()

get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    get_target_property(target_type ${tgt} TYPE)
    if (target_type STREQUAL "EXECUTABLE")
        target_link_libraries(${tgt} cio::cio)
        set_target_properties(${tgt} PROPERTIES
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
    endif()
	if(CIO_ENABLE_LTO)
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endforeach()
