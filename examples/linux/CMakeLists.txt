cmake_minimum_required(VERSION 3.9)
project(cio-examples-linux C)

add_executable(uds_socket_ping_pong
    uds_socket_ping_pong.c
)
target_link_libraries(uds_socket_ping_pong)

get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    get_target_property(target_type ${tgt} TYPE)
    if (target_type STREQUAL "EXECUTABLE")
        target_link_libraries(${tgt} cio:cio)
        set_target_properties(${tgt} PROPERTIES
    		C_STANDARD_REQUIRED ON
    		C_EXTENSIONS OFF
        )
    endif()
	if(CIO_ENABLE_LTO)
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endforeach()
