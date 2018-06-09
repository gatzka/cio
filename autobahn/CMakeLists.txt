cmake_minimum_required(VERSION 3.0)
project(cio-autobahn C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake/)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

include(sanitizer)

if (NOT TARGET cio_static)
    add_subdirectory(../src/ lib)
endif()

set(WARN_SWITCHES "-Wall -Wextra -Werror -Wshadow -Winit-self -Wcast-qual -Wcast-align -Wformat=2 -Wwrite-strings -Wmissing-prototypes -Wstrict-overflow=5 -pedantic")
set(CMAKE_C_FLAGS "-pipe -fno-common -std=c11 ${WARN_SWITCHES} ${CMAKE_C_FLAGS}")

add_executable(autobahn_server
    autobahn_server.c
)
target_link_libraries (autobahn_server cio_static)

