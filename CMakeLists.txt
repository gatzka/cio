cmake_minimum_required(VERSION 3.0)
project(cioall C)

enable_testing()
include(CTest)

add_subdirectory(src)
add_subdirectory(tests)

add_subdirectory(examples)
add_subdirectory(autobahn)

