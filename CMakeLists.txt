cmake_minimum_required(VERSION 3.0)
project(cioall C)

add_subdirectory(src/) 
add_subdirectory(src/linux/tests/) 
add_subdirectory(src/tests/) 
add_subdirectory(examples/) 

enable_testing()

