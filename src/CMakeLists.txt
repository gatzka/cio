cmake_minimum_required(VERSION 3.0)
project(cio C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake/)
include(generateVersion)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

include(sanitizer)
GenerateVersion(version ${PROJECT_NAME})

configure_file(cio_version.h.in ${PROJECT_BINARY_DIR}/generated/cio_version.h)

string(COMPARE EQUAL "${CMAKE_SYSTEM_NAME}" "Linux" is_linux)
if(is_linux)
    set(CIO_LINUX_FILES
        linux/cio_linux_endian.c
        linux/cio_linux_epoll.c
        linux/cio_linux_random.c
        linux/cio_linux_server_socket.c
        linux/cio_linux_socket.c
        linux/cio_linux_socket_utils.c
        linux/cio_linux_string.c
        linux/cio_linux_timer.c
    )

    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/linux/)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/)
set(CIO_FILES
    cio_base64.c
    cio_buffered_stream.c
	cio_http_location.c
	cio_http_location_handler.c
	cio_http_server.c
	cio_websocket.c
	cio_websocket_location_handler.c
	cio_websocket_masking.c
	http-parser/http_parser.c
    sha1/sha1.c
)

set_property(SOURCE http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wstrict-overflow=2 ")

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    if(NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 7.0.0)
        set_property(SOURCE http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wimplicit-fallthrough=0 ")
    endif()
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(WARN_SWITCHES "-Wall -Wextra -Werror -Wshadow -Winit-self -Wcast-qual -Wcast-align -Wformat=2 -Wwrite-strings -Wmissing-prototypes -Wstrict-overflow=5 -pedantic")

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        if(NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 4.5.0)
            set(WARN_SWITCHES "${WARN_SWITCHES} -Wunused-result")
        endif()
    endif()

    set(CMAKE_C_FLAGS "-pipe -fno-common ${WARN_SWITCHES} ${CMAKE_C_FLAGS}")
    set(CMAKE_C_FLAGS_RELEASE "-fno-asynchronous-unwind-tables ${CMAKE_C_FLAGS_RELEASE}")

    foreach(_file ${CIO_LINUX_FILES})
        file(SHA1 ${PROJECT_SOURCE_DIR}/${_file} checksum)
        string(SUBSTRING ${checksum} 0 8 checksum)
        set_property(SOURCE ${_file} APPEND_STRING PROPERTY COMPILE_FLAGS "-std=c99 -frandom-seed=0x${checksum}")
    endforeach()

    foreach(_file ${CIO_FILES})
        file(SHA1 ${PROJECT_SOURCE_DIR}/${_file} checksum)
        string(SUBSTRING ${checksum} 0 8 checksum)
        set_property(SOURCE ${_file} APPEND_STRING PROPERTY COMPILE_FLAGS "-std=c99 -frandom-seed=0x${checksum}")
    endforeach()
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wdocumentation")
endif()

add_library(cio_static
    ${CIO_FILES}
    ${CIO_LINUX_FILES}
)
set_target_properties(cio_static
    PROPERTIES OUTPUT_NAME "cio"
)

add_library(cio SHARED
    ${CIO_FILES}
    ${CIO_LINUX_FILES}
)
set_target_properties(cio
    PROPERTIES OUTPUT_NAME "cio"
)

file(GLOB LIB_HEADERS
   ${PROJECT_SOURCE_DIR}/*.h
)
file(GLOB LIB_HEADERS_LINUX
   ${PROJECT_SOURCE_DIR}/linux/*.h
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
    FILES ${LIB_HEADERS} DESTINATION include/cio/
)
install(
    FILES ${LIB_HEADERS_LINUX} DESTINATION include/cio/linux/
)
install(
    FILES ${PROJECT_BINARY_DIR}/generated/cio_version.h DESTINATION include/cio/
)

