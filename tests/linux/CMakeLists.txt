cmake_minimum_required(VERSION 3.9)
project(cio_linux_unit_test C)
enable_testing()

add_executable(test_cio_linux_endian
    test_cio_linux_endian.c
    ../../lib/src/platform/linux/cio_linux_endian.c
)

add_executable(test_cio_linux_epoll
    test_cio_linux_epoll.c
    ../../lib/src/platform/linux/cio_linux_epoll.c
)

add_executable(test_cio_linux_random_errors
    test_cio_linux_random_errors.c
    ../../lib/src/platform/linux/cio_linux_random.c
    ../../lib/src/cio_random.c
)

add_executable(test_cio_linux_server_socket
    ../../lib/src/platform/linux/cio_linux_endian.c
    ../../lib/src/platform/linux/cio_linux_server_socket.c
    ../../lib/src/platform/linux/cio_linux_socket_utils.c
    ../../lib/src/platform/linux/cio_unix_address.c
    ../../lib/src/platform/shared/cio_inet_address_impl.c
    ../../lib/src/platform/shared/cio_socket_address_impl.c
    test_cio_linux_server_socket.c
)

add_executable(test_cio_linux_socket
    ../../lib/src/platform/linux/cio_linux_endian.c
    ../../lib/src/platform/linux/cio_linux_socket.c
    ../../lib/src/platform/linux/cio_linux_socket_utils.c
    ../../lib/src/platform/shared/cio_inet_address_impl.c
    ../../lib/src/platform/shared/cio_socket_address_impl.c
    test_cio_linux_socket.c
)

add_executable(test_cio_linux_socket_utils
    test_cio_linux_socket_utils.c
    ../../lib/src/platform/linux/cio_linux_socket_utils.c
)

add_executable(test_cio_linux_string
    test_cio_linux_string.c
    ../../lib/src/platform/linux/cio_linux_string.c
)

add_executable(test_cio_linux_timer
    test_cio_linux_timer.c
    ../../lib/src/platform/linux/cio_linux_timer.c
    ../../lib/src/platform/linux/cio_linux_socket_utils.c
)

add_executable(test_cio_linux_unix_socket_address
    test_cio_linux_unix_socket_address.c
    ../../lib/src/platform/linux/cio_unix_address.c
)

add_executable(test_cio_linux_uart
    ../../lib/src/platform/linux/cio_linux_socket_utils.c
    ../../lib/src/platform/linux/cio_linux_uart.c
    test_cio_linux_uart.c)

get_property(includes TARGET cio::cio PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    get_target_property(target_type ${tgt} TYPE)
    if (target_type STREQUAL "EXECUTABLE")
        add_dependencies(${tgt} cio::cio) # Necessary to generage cio_version_private.h before compiling the tests.
        target_link_libraries(${tgt} fff unity)
        set_target_properties(${tgt} PROPERTIES
            C_CLANG_TIDY ""
            C_STANDARD 11
    	    C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        target_include_directories(${tgt} PUBLIC ${includes})
        add_test(NAME cio.linux.${tgt} COMMAND ${tgt})
    endif()

    if(CIO_ENABLE_LTO AND NOT ${target_type} STREQUAL "INTERFACE_LIBRARY")
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endforeach()
