cmake_minimum_required(VERSION 3.0)
project(cio-autobahn C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake/)
include(compiler_settings)
include(sanitizer)

if (NOT TARGET cio_static)
    add_subdirectory(../src/ lib)
endif()

add_executable(autobahn_server
    autobahn_server.c
)
target_link_libraries (autobahn_server cio_static)

