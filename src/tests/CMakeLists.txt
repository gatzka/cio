cmake_minimum_required(VERSION 3.0)
project(cio-tests C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../../cmake/)
include(compiler_settings)
include(generateVersion)
include(sanitizer)

GenerateVersion(../version ${PROJECT_NAME})
configure_file(../cio_version.h.in ${PROJECT_BINARY_DIR}/generated/cio_version.h)

string(COMPARE EQUAL "${CMAKE_SYSTEM_NAME}" "Linux" is_linux)
string(COMPARE EQUAL "${CMAKE_SYSTEM_NAME}" "Windows" is_windows)

add_library(cio_unity
    ../unity/src/unity.c
    ../unity/src/unity.h
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)
if (is_linux)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../linux/)
elseif(is_windows)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../windows/)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../fff/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../unity/src/)
include_directories(${PROJECT_BINARY_DIR}/generated/)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
	if(CMAKE_BUILD_TYPE MATCHES Debug)
	set(COVERAGE_FLAGS "-fprofile-arcs -ftest-coverage")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_FLAGS}")
	endif(CMAKE_BUILD_TYPE MATCHES Debug)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    if(NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 7.0.0)
        set_property(SOURCE ../http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wimplicit-fallthrough=0 ")
    endif()
    set_property(SOURCE ../http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wstrict-overflow=2 -Wno-switch-default")
    set_property(SOURCE ../unity/src/unity.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wstrict-overflow=2 ")
endif()

add_executable(test_cio_base64
    test_cio_base64.c
	../cio_base64.c
)
target_link_libraries (test_cio_base64 cio_unity)

if (is_linux)
	set (TEST_CIO_BUFFERED_STREAM_PLATFORM_FILES
	    ../linux/cio_linux_string.c
	)
elseif (is_windows)
	set (TEST_CIO_BUFFERED_STREAM_PLATFORM_FILES
	    ../windows/cio_windows_string.c
	)
endif()

add_executable(test_cio_buffered_stream
    test_cio_buffered_stream.c
    ../cio_buffered_stream.c
	${TEST_CIO_BUFFERED_STREAM_PLATFORM_FILES}
)
target_link_libraries (test_cio_buffered_stream cio_unity)

add_executable(test_cio_http_server_iostream
    test_cio_http_server_iostream.c
	../cio_http_location.c
	../cio_http_location_handler.c
    ../cio_buffered_stream.c
    ../cio_http_server.c
    ../http-parser/http_parser.c
    ${TEST_CIO_BUFFERED_STREAM_PLATFORM_FILES}
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

add_executable(test_cio_utf8_checker
    test_cio_utf8_checker.c
	../cio_utf8_checker.c
)
target_link_libraries (test_cio_utf8_checker cio_unity)

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
target_link_libraries (test_cio_websocket_mask cio_unity bsd)

#add_executable(test_cio_websocket_location_handler
#    test_cio_websocket_location_handler.c
#    ../cio_base64.c
#	../cio_http_location.c
#	../cio_http_location_handler.c
#    ../cio_http_server.c
#    ../cio_utf8_checker.c
#    ../cio_websocket.c
#    ../cio_websocket_location_handler.c
#    ../http-parser/http_parser.c
#    ../linux/cio_linux_endian.c
#    ../linux/cio_linux_random.c
#    ../linux/cio_linux_string.c
#    ../sha1/sha1.c
#)
#target_link_libraries (test_cio_websocket_location_handler cio_unity bsd)

#add_executable(test_cio_websocket
#    test_cio_websocket.c
#    ../cio_utf8_checker.c
#    ../cio_websocket.c
#    ../linux/cio_linux_endian.c
#    ../linux/cio_linux_random.c
#)
#target_link_libraries (test_cio_websocket cio_unity bsd)

#add_executable(test_cio_websocket_client
#    test_cio_websocket_client.c
#    ../cio_utf8_checker.c
#    ../cio_websocket.c
#    ../linux/cio_linux_endian.c
#    ../linux/cio_linux_random.c
#)
#target_link_libraries (test_cio_websocket_client cio_unity bsd)

enable_testing()
add_test(NAME test_cio_base64 COMMAND test_cio_base64)
add_test(NAME test_cio_buffered_stream COMMAND test_cio_buffered_stream)
add_test(NAME test_cio_http_server COMMAND test_cio_http_server)
add_test(NAME test_cio_http_server_iostream COMMAND test_cio_http_server_iostream)
add_test(NAME test_cio_read_buffer COMMAND test_cio_read_buffer)
add_test(NAME test_cio_utf8_checker COMMAND test_cio_utf8_checker)
#add_test(NAME test_cio_websocket COMMAND test_cio_websocket)
#add_test(NAME test_cio_websocket_client COMMAND test_cio_websocket_client)
#add_test(NAME test_cio_websocket_location_handler COMMAND test_cio_websocket_location_handler)
add_test(NAME test_cio_websocket_mask COMMAND test_cio_websocket_mask)
add_test(NAME test_cio_write_buffer COMMAND test_cio_write_buffer)


