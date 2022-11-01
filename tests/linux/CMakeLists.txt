cmake_minimum_required(VERSION 3.9)
project(cio_linux_unit_test C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
enable_testing()

set_source_files_properties(
    ../../lib/src/platform/linux/endian.c
    ../../lib/src/platform/linux/random.c
    PROPERTIES COMPILE_DEFINITIONS _DEFAULT_SOURCE
)

set_source_files_properties(
    ../../lib/src/platform/linux/server_socket.c
    ../../lib/src/platform/linux/socket.c
    ../../lib/src/platform/linux/string.c
    ../../lib/src/platform/linux/timer.c
    ../../lib/src/platform/linux/uart.c
    PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE
)

add_executable(test_linux_endian
    test_linux_endian.c
    ../../lib/src/platform/linux/endian.c
)
add_executable(test_linux_epoll
    test_linux_epoll.c
    ../../lib/src/platform/linux/epoll.c
)

add_executable(test_linux_random_errors
    test_linux_random_errors.c
    ../../lib/src/platform/linux/random.c
    ../../lib/src/random.c
)

add_executable(test_linux_server_socket
    ../../lib/src/platform/linux/endian.c
    ../../lib/src/platform/linux/server_socket.c
    ../../lib/src/platform/linux/socket_utils.c
    ../../lib/src/platform/linux/unix_address.c
    ../../lib/src/platform/shared/inet_address_impl.c
    ../../lib/src/platform/shared/socket_address_impl.c
    test_linux_server_socket.c
)

add_executable(test_linux_socket
    ../../lib/src/platform/linux/endian.c
    ../../lib/src/platform/linux/socket.c
    ../../lib/src/platform/linux/socket_utils.c
    ../../lib/src/platform/shared/inet_address_impl.c
    ../../lib/src/platform/shared/socket_address_impl.c
    test_linux_socket.c
)

add_executable(test_linux_socket_utils
    test_linux_socket_utils.c
    ../../lib/src/platform/linux/socket_utils.c
)

add_executable(test_linux_string
    test_linux_string.c
    ../../lib/src/platform/linux/string.c
)

add_executable(test_linux_timer
    test_linux_timer.c
    ../../lib/src/platform/linux/timer.c
    ../../lib/src/platform/linux/socket_utils.c
)

add_executable(test_linux_unix_socket_address
    test_linux_unix_socket_address.c
    ../../lib/src/platform/linux/unix_address.c
)

add_executable(test_linux_uart
    ../../lib/src/platform/linux/socket_utils.c
    ../../lib/src/platform/linux/uart.c
    test_linux_uart.c
)

get_property(includes TARGET cio::cio PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    get_target_property(target_type ${tgt} TYPE)
    if (target_type STREQUAL "EXECUTABLE")
        add_dependencies(${tgt} cio::cio) # Necessary to generage version_private.h before compiling the tests.
        target_link_libraries(${tgt} fff unity)
        set_target_properties(${tgt} PROPERTIES
            C_CLANG_TIDY ""
            C_STANDARD 11
    	    C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        target_include_directories(${tgt} PUBLIC ${includes})
        add_test(NAME linux.${tgt} COMMAND ${tgt})
    endif()

    if(CIO_ENABLE_LTO AND NOT ${target_type} STREQUAL "INTERFACE_LIBRARY")
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endforeach()
