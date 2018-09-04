cmake_minimum_required(VERSION 3.0)
project(cioall C)

message(STATUS "build type: " ${CMAKE_BUILD_TYPE})

enable_testing()

add_subdirectory(src/) 
add_subdirectory(examples/) 
add_subdirectory(autobahn/) 

