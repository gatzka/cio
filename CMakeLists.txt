cmake_minimum_required(VERSION 3.0)
project(cioall C)

include(CTest)

add_subdirectory(src)
add_subdirectory(tests)

add_subdirectory(examples)
add_subdirectory(autobahn)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/)

include(ClangFormat)

file(GLOB FILES_TO_FORMAT
   ${PROJECT_SOURCE_DIR}/src/*.c
   ${PROJECT_SOURCE_DIR}/src/*.h
   ${PROJECT_SOURCE_DIR}/src/linux/*.c
   ${PROJECT_SOURCE_DIR}/src/linux/*.h
)

add_custom_target(cio-format SOURCES ${FILES_TO_FORMAT})
AddClangFormat(cio-format)
