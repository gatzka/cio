cmake_minimum_required(VERSION 3.0)
project(cio-tests C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../../cmake/)
include(generateVersion)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

GenerateVersion(../version ${PROJECT_NAME})
configure_file(../cio_version.h.in ${PROJECT_BINARY_DIR}/generated/cio_version.h)

include(sanitizer)

add_library(cio_unity
    ../unity/src/unity.c
    ../unity/src/unity.h
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../linux/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../fff/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../unity/src/)
include_directories(${PROJECT_BINARY_DIR}/generated/)

set(WARN_SWITCHES "-Wall -Wextra -Werror -Wshadow -Winit-self -Wcast-qual -Wcast-align -Wformat=2 -Wwrite-strings -Wmissing-prototypes -Wstrict-overflow=5 -pedantic")
set(COVERAGE_FLAGS "-fprofile-arcs -ftest-coverage")
set(CMAKE_C_FLAGS "-pipe -fno-common -std=c11 ${WARN_SWITCHES} ${CMAKE_C_FLAGS}")

if(CMAKE_BUILD_TYPE MATCHES Debug)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_FLAGS}")
endif(CMAKE_BUILD_TYPE MATCHES Debug)

message(STATUS "CMAKE_C_COMPILER_ID: " ${CMAKE_C_COMPILER_ID})
message(STATUS "CMAKE_C_COMPILER_VERSION: " ${CMAKE_C_COMPILER_VERSION})

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    if(NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 7.0.0)
        set_property(SOURCE ../http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wimplicit-fallthrough=0 ")
    endif()
    set_property(SOURCE ../http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wstrict-overflow=2 ")
endif()

add_executable(test_cio_buffered_stream
    test_cio_buffered_stream.c
    ../cio_buffered_stream.c
    ../linux/cio_linux_string.c
)
target_link_libraries (test_cio_buffered_stream cio_unity)

add_executable(test_cio_http_server_iostream
    test_cio_http_server_iostream.c
	../cio_http_location.c
	../cio_http_location_handler.c
    ../cio_buffered_stream.c
    ../cio_http_server.c
    ../http-parser/http_parser.c
    ../linux/cio_linux_string.c
)
target_link_libraries (test_cio_http_server_iostream cio_unity)

add_executable(test_cio_http_server
    test_cio_http_server.c
	../cio_http_location.c
	../cio_http_location_handler.c
    ../cio_http_server.c
    ../http-parser/http_parser.c
)
target_link_libraries (test_cio_http_server cio_unity)

add_executable(test_cio_base64
    test_cio_base64.c
	../cio_base64.c
)
target_link_libraries (test_cio_base64 cio_unity)

add_executable(test_cio_read_buffer
    test_cio_read_buffer.c
)
target_link_libraries (test_cio_read_buffer cio_unity)

add_executable(test_cio_write_buffer
    test_cio_write_buffer.c
)
target_link_libraries (test_cio_write_buffer cio_unity)

add_executable(test_cio_websocket_mask
    test_cio_websocket_mask.c
)
target_link_libraries (test_cio_websocket_mask cio_unity)

add_executable(test_cio_websocket_location_handler
    test_cio_websocket_location_handler.c
    ../cio_base64.c
	../cio_http_location.c
	../cio_http_location_handler.c
    ../cio_http_server.c
    ../cio_utf8_checker.c
    ../cio_websocket.c
    ../cio_websocket_location_handler.c
    ../http-parser/http_parser.c
    ../linux/cio_linux_endian.c
    ../linux/cio_linux_random.c
    ../linux/cio_linux_string.c
    ../sha1/sha1.c
)
target_link_libraries (test_cio_websocket_location_handler cio_unity)

add_executable(test_cio_websocket
    test_cio_websocket.c
    ../cio_utf8_checker.c
    ../cio_websocket.c
    ../linux/cio_linux_endian.c
)
target_link_libraries (test_cio_websocket cio_unity)

enable_testing()
add_test(NAME test_cio_base64 COMMAND test_cio_base64)
add_test(NAME test_cio_buffered_stream COMMAND test_cio_buffered_stream)
add_test(NAME test_cio_http_server COMMAND test_cio_http_server)
add_test(NAME test_cio_http_server_iostream COMMAND test_cio_http_server_iostream)
add_test(NAME test_cio_read_buffer COMMAND test_cio_read_buffer)
add_test(NAME test_cio_websocket COMMAND test_cio_websocket)
add_test(NAME test_cio_websocket_location_handler COMMAND test_cio_websocket_location_handler)
add_test(NAME test_cio_websocket_mask COMMAND test_cio_websocket_mask)
add_test(NAME test_cio_write_buffer COMMAND test_cio_write_buffer)


