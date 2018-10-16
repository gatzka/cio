cmake_minimum_required(VERSION 3.0)
project(cioall C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/)

message(STATUS "build type: " ${CMAKE_BUILD_TYPE})

enable_testing()

add_subdirectory(src/) 
add_subdirectory(examples/) 
add_subdirectory(autobahn/) 


include(ClangFormat)

file(GLOB FILES_TO_FORMAT
       ${PROJECT_SOURCE_DIR}/src/*.c
       ${PROJECT_SOURCE_DIR}/src/*.h
       ${PROJECT_SOURCE_DIR}/src/linux/*.c
       ${PROJECT_SOURCE_DIR}/src/linux/*.h
)

add_custom_target(cio-files SOURCES ${FILES_TO_FORMAT})
AddClangFormat(cio-files)
