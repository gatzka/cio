cmake_minimum_required(VERSION 3.0)
project(cio C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake/)
include(compiler_settings)
include(generateVersion)
include(sanitizer)

GenerateVersion(version ${PROJECT_NAME})

configure_file(cio_version.h.in ${PROJECT_BINARY_DIR}/generated/cio_version.h)

find_program(
  CLANG_TIDY_EXE
  NAMES "clang-tidy"
  DOC "Path to clang-tidy executable"
  )
if(NOT CLANG_TIDY_EXE)
  message(STATUS "clang-tidy not found.")
else()
  message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
  set(DO_CLANG_TIDY "${CLANG_TIDY_EXE}" "-extra-arg=-Wno-unknown-warning-option")
endif()

string(COMPARE EQUAL "${CMAKE_SYSTEM_NAME}" "Linux" is_linux)
if(is_linux)
    set(CIO_PLATFORM_FILES
        "linux/cio_linux_endian.c"
        "linux/cio_linux_epoll.c"
        "linux/cio_linux_random.c"
        "linux/cio_linux_server_socket.c"
        "linux/cio_linux_socket.c"
        "linux/cio_linux_socket_utils.c"
        "linux/cio_linux_string.c"
        "linux/cio_linux_timer.c"
    )

    add_library(target_platform
        OBJECT
    	${CIO_PLATFORM_FILES}
    )

    set_property(TARGET target_platform PROPERTY POSITION_INDEPENDENT_CODE 1)
endif()

set(CIO_FILES
    "cio_base64.c"
    "cio_buffered_stream.c"
    "cio_http_location.c"
    "cio_http_location_handler.c"
    "cio_http_server.c"
    "cio_utf8_checker.c"
    "cio_websocket.c"
    "cio_websocket_location_handler.c"
    "sha1/sha1.c"
)

add_library(target_platform_independent
    OBJECT
	${CIO_FILES}
)

set_property(TARGET target_platform_independent PROPERTY POSITION_INDEPENDENT_CODE 1)

add_library(
    target_third_party
    OBJECT
    "http-parser/http_parser.c"
    "http-parser/http_parser.h"
)
set_property(TARGET target_third_party PROPERTY POSITION_INDEPENDENT_CODE 1)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    if(NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 7.0.0)
        set_property(SOURCE http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wimplicit-fallthrough=0 ")
    endif()
    set_property(SOURCE http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wstrict-overflow=2 -Wno-switch-default ")
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")

    foreach(_file ${CIO_PLATFORM_FILES})
        file(SHA1 ${CMAKE_CURRENT_SOURCE_DIR}/${_file} checksum)
        string(SUBSTRING ${checksum} 0 8 checksum)
        set_property(SOURCE ${_file} APPEND_STRING PROPERTY COMPILE_FLAGS "-frandom-seed=0x${checksum}")
    endforeach()

    foreach(_file ${CIO_FILES})
        file(SHA1 ${CMAKE_CURRENT_SOURCE_DIR}/${_file} checksum)
        string(SUBSTRING ${checksum} 0 8 checksum)
        set_property(SOURCE ${_file} APPEND_STRING PROPERTY COMPILE_FLAGS "-frandom-seed=0x${checksum}")
    endforeach()
endif()

set_target_properties(
    target_third_party
    PROPERTIES C_CLANG_TIDY ""
)

add_library(cio_static
    $<TARGET_OBJECTS:target_platform_independent>
    $<TARGET_OBJECTS:target_platform>
    $<TARGET_OBJECTS:target_third_party>
)

set_target_properties(cio_static
    PROPERTIES OUTPUT_NAME "cio"
)

set_target_properties(
    target_platform_independent PROPERTIES
    C_CLANG_TIDY "${DO_CLANG_TIDY}"
)

set_target_properties(
    target_platform PROPERTIES
    C_CLANG_TIDY "${DO_CLANG_TIDY}"
)

target_include_directories(target_platform_independent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)
target_include_directories(target_platform_independent PUBLIC ${PROJECT_BINARY_DIR}/generated/)
if(is_linux)
    target_include_directories(target_platform_independent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux/)
    target_include_directories(target_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux/)
    target_include_directories(target_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)
endif()
target_link_libraries(cio_static bsd)

target_include_directories(cio_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)
target_include_directories(cio_static PUBLIC ${PROJECT_BINARY_DIR}/generated/)
if(is_linux)
    target_include_directories(cio_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux/)
endif()

add_library(cio SHARED
    $<TARGET_OBJECTS:target_platform_independent>
    $<TARGET_OBJECTS:target_platform>
    $<TARGET_OBJECTS:target_third_party>
)
set_target_properties(cio
    PROPERTIES OUTPUT_NAME "cio"
)
target_include_directories(cio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)
target_include_directories(cio PUBLIC ${PROJECT_BINARY_DIR}/generated/)
target_link_libraries(cio bsd)
if(is_linux)
    target_include_directories(cio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux/)
endif()

set(LIB_HEADERS
	"cio_base64.h"
	"cio_buffered_stream.h"
	"cio_compiler.h"
	"cio_endian.h"
	"cio_error_code.h"
	"cio_eventloop.h"
	"cio_http_client.h"
	"cio_http_location.h"
	"cio_http_location_handler.h"
	"cio_http_method.h"
	"cio_http_server.h"
	"cio_http_status_code.h"
	"cio_io_stream.h"
	"cio_random.h"
	"cio_read_buffer.h"
	"cio_server_socket.h"
	"cio_socket.h"
	"cio_string.h"
	"cio_timer.h"
	"cio_utf8_checker.h"
	"cio_util.h"
	"cio_websocket.h"
	"cio_websocket_location_handler.h"
	"cio_websocket_masking.h"
	"cio_write_buffer.h"
    "http-parser/http_parser.h"
    "sha1/sha1.h"
)

set(LIB_HEADERS_LINUX
    "linux/cio_error_code_impl.h"
    "linux/cio_eventloop_impl.h"
    "linux/cio_linux_socket.h"
    "linux/cio_linux_socket_utils.h"
)

set_target_properties(cio
    PROPERTIES VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}" SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
    TARGETS cio_static cio
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/${LIB_HEADERS} DESTINATION include/cio/
)
install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/${LIB_HEADERS_LINUX} DESTINATION include/cio/linux/
)
install(
    FILES ${PROJECT_BINARY_DIR}/generated/cio_version.h DESTINATION include/cio/
)

