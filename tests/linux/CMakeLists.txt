cmake_minimum_required(VERSION 3.9)
project(cio_linux_unit_test C)
enable_testing()

add_executable(test_cio_linux_endian test_cio_linux_endian.c)
add_test(NAME cio.linux.test_endian COMMAND test_cio_linux_endian)
 
add_executable(test_cio_linux_epoll test_cio_linux_epoll.c)
add_test(NAME cio.linux.test_epoll COMMAND test_cio_linux_epoll)

add_executable(test_cio_linux_random_errors test_cio_linux_random_errors.c)
add_test(NAME cio.common.test_linux_random_errors COMMAND test_cio_linux_random_errors)

add_executable(test_cio_linux_server_socket test_cio_linux_server_socket.c)
add_test(NAME cio.linux.test_server_socket COMMAND test_cio_linux_server_socket)
 
add_executable(test_cio_linux_socket test_cio_linux_socket.c)
add_test(NAME cio.linux.test_socket COMMAND test_cio_linux_socket)

add_executable(test_cio_linux_socket_utils test_cio_linux_socket_utils.c)
add_test(NAME cio.linux.test_socket_utils COMMAND test_cio_linux_socket_utils)

add_executable(test_cio_linux_string test_cio_linux_string.c)
add_test(NAME cio.linux.test_string COMMAND test_cio_linux_string)
 
add_executable(test_cio_linux_timer test_cio_linux_timer.c)
add_test(NAME cio.linux.test_timer COMMAND test_cio_linux_timer)

get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    get_target_property(target_type ${tgt} TYPE)
    if (target_type STREQUAL "EXECUTABLE")
        target_link_libraries(${tgt} fff unity cio:cio)
        set_target_properties(${tgt} PROPERTIES
            C_CLANG_TIDY ""
            C_STANDARD 11
    	    C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
    endif()

    if(CIO_ENABLE_LTO AND NOT ${target_type} STREQUAL "INTERFACE_LIBRARY")
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endforeach()
