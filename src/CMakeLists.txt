cmake_minimum_required(VERSION 3.0)
project(cio C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake/)
include(generateVersion)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

include(sanitizer)
GenerateVersion(version)

configure_file(cio_version.h.in ${PROJECT_BINARY_DIR}/generated/cio_version.h)

string(COMPARE EQUAL "${CMAKE_SYSTEM_NAME}" "Linux" is_linux)
if(is_linux)
    set(CIO_LINUX_FILES
        linux/cio_linux_epoll.c
        linux/cio_linux_server_socket.c
    )
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ ${CMAKE_CURRENT_SOURCE_DIR}/linux/)
set(CIO_FILES
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(WARN_SWITCHES "-Wall -Wextra -Werror -Wshadow -Winit-self -Wcast-qual -Wcast-align -Wformat=2 -Wwrite-strings -Wmissing-prototypes -Wstrict-overflow=5 -Wdouble-promotion -pedantic")
    set(CMAKE_C_FLAGS "-pipe -fno-common ${WARN_SWITCHES} ${CMAKE_C_FLAGS}")
    set(CMAKE_C_FLAGS_RELEASE "-fno-asynchronous-unwind-tables ${CMAKE_C_FLAGS_RELEASE}")
    set_source_files_properties(
        ${CIO_FILES} PROPERTIES COMPILE_FLAGS "-std=c99"
    )
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

add_subdirectory(linux/tests/) 

enable_testing()

