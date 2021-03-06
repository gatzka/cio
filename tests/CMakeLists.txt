cmake_minimum_required(VERSION 3.9)
project(cio_unit_test C)
enable_testing()

add_library(fff INTERFACE)
target_include_directories(fff INTERFACE fff)

add_library(unity OBJECT
    unity/src/unity.c
)
set_target_properties(unity PROPERTIES
    COMPILE_OPTIONS "$<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wno-strict-overflow>"
    C_CLANG_TIDY ""
)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_LIST_DIR}/unity/src/)

set_source_files_properties(
    ../lib/src/platform/linux/string.c
    PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE
)

add_executable(test_base64
    test_base64.c
    ../lib/src/base64.c
)

add_executable(test_buffered_stream
    test_buffered_stream.c
    ../lib/src/buffered_stream.c
    ../lib/src/platform/shared/string_memmem.c
)

set_source_files_properties(../lib/cio/http-parser/http_parser.c
    PROPERTIES COMPILE_OPTIONS
        "$<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wno-shadow;-Wno-sign-conversion;-Wno-conversion;-Wno-switch-default>"
)
add_executable(test_http_server
    test_http_server.c
    ../lib/src/http_server.c
    ../lib/src/http_location.c
    ../lib/src/http_location_handler.c
    ../lib/cio/http-parser/http_parser.c)

add_executable(test_http_location
    test_http_location.c
    ../lib/src/http_location.c
    ../lib/src/http_location_handler.c
)

add_executable(test_inet_address
    test_inet_address.c
    ../lib/src/platform/shared/inet_address_impl.c
)

add_executable(test_inet_socket_address
    test_inet_socket_address.c
    ../lib/src/platform/shared/inet_address_impl.c
    ../lib/src/platform/shared/socket_address_impl.c
    $<$<PLATFORM_ID:Linux>:../lib/src/platform/linux/endian.c>
    $<$<PLATFORM_ID:Windows>:../lib/src/platform/windows/endian.c>
)
set_source_files_properties(
    ../lib/src/platform/linux/endian.c
    PROPERTIES COMPILE_DEFINITIONS _DEFAULT_SOURCE
)

add_executable(test_random
    test_random.c
    ../lib/src/random.c
)

add_executable(test_read_buffer
    test_read_buffer.c
)
 
add_executable(test_utf8_checker
    test_utf8_checker.c
    ../lib/src/utf8_checker.c
)

add_executable(test_websocket
    test_websocket.c
    ../lib/src/websocket.c
)

add_executable(test_websocket_location_handler
    test_websocket_location_handler.c
    ../lib/src/base64.c
    ../lib/src/websocket_location_handler.c
    ../lib/cio/sha1/sha1.c
    $<$<PLATFORM_ID:Linux>:../lib/src/platform/linux/string.c>
    $<$<PLATFORM_ID:Windows>:../lib/src/platform/windows/string.c>
)

add_executable(test_websocket_mask
    test_websocket_mask.c
)

add_executable(test_write_buffer
    test_write_buffer.c
)

add_executable(test_version
    test_version.c
    ../lib/src/version.c
)

get_property(includes TARGET cio::cio PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    get_target_property(target_type ${tgt} TYPE)
    if (target_type STREQUAL "EXECUTABLE")
        add_dependencies(${tgt} cio::cio) # Necessary to generage version_private.h before compiling the tests.
        target_link_libraries(${tgt} fff unity)
        set_target_properties(${tgt} PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
            C_CLANG_TIDY ""
        )
        target_include_directories(${tgt} PUBLIC ${includes})
        add_test(NAME common.${tgt} COMMAND ${tgt})
    endif()

	if(CIO_ENABLE_LTO AND NOT ${target_type} STREQUAL "INTERFACE_LIBRARY")
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endforeach()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_subdirectory(linux)
endif()

