cmake_minimum_required(VERSION 3.0)
project(cio C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake/)
include(compiler_settings)
include(generateVersion)
include(sanitizer)

GenerateVersion(version ${PROJECT_NAME})

configure_file(cio_version.h.in ${PROJECT_BINARY_DIR}/generated/cio_version.h)

string(COMPARE EQUAL "${CMAKE_SYSTEM_NAME}" "Linux" is_linux)
if(is_linux)
    file(GLOB CIO_LINUX_FILES
        "linux/*.c"
        "linux/*.h"
    )
endif()

file(GLOB CIO_FILES
    "*.c"
    "*.h"
    "http-parser/http_parser.c"
    "http-parser/http_parser.h"
    "sha1/*.c"
    "sha1/*.h"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    if(NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 7.0.0)
        set_property(SOURCE http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wimplicit-fallthrough=0 ")
    endif()
    set_property(SOURCE http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wstrict-overflow=2 -Wno-switch-default ")
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")

    foreach(_file ${CIO_LINUX_FILES})
        file(SHA1 ${_file} checksum)
        string(SUBSTRING ${checksum} 0 8 checksum)
        set_property(SOURCE ${_file} APPEND_STRING PROPERTY COMPILE_FLAGS "-frandom-seed=0x${checksum}")
    endforeach()

    foreach(_file ${CIO_FILES})
        file(SHA1 ${_file} checksum)
        string(SUBSTRING ${checksum} 0 8 checksum)
        set_property(SOURCE ${_file} APPEND_STRING PROPERTY COMPILE_FLAGS "-frandom-seed=0x${checksum}")
    endforeach()
endif()

add_library(cio_static
    ${CIO_FILES}
    ${CIO_LINUX_FILES}
)
set_target_properties(cio_static
    PROPERTIES OUTPUT_NAME "cio"
)
target_include_directories(cio_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)
target_include_directories(cio_static PUBLIC ${PROJECT_BINARY_DIR}/generated/)
if(is_linux)
    target_include_directories(cio_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux/)
endif()
target_link_libraries(cio_static bsd)

add_library(cio SHARED
    ${CIO_FILES}
    ${CIO_LINUX_FILES}
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

