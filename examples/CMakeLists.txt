cmake_minimum_required(VERSION 3.0)
project(cio-tests C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake/)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

include(sanitizer)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src/ ${CMAKE_CURRENT_SOURCE_DIR}/../src/linux/)

set(WARN_SWITCHES "-Wall -Wextra -Werror -Wshadow -Winit-self -Wcast-qual -Wcast-align -Wformat=2 -Wwrite-strings -Wmissing-prototypes -Wstrict-overflow=5 -pedantic")
set(CMAKE_C_FLAGS "-pipe -fno-common -std=gnu99 ${WARN_SWITCHES} ${CMAKE_C_FLAGS}")

add_executable(socket_echo
    socket_echo.c
)
target_link_libraries (socket_echo cio_static)

add_executable(timer_example
    timer_example.c
)
target_link_libraries (timer_example cio_static)

