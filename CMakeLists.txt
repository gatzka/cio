cmake_minimum_required(VERSION 3.0)
project(cioall C)

string(COMPARE EQUAL "${CMAKE_SYSTEM_NAME}" "Linux" is_linux)
string(COMPARE EQUAL "${CMAKE_SYSTEM_NAME}" "Windows" is_windows)

add_subdirectory(src/) 
add_subdirectory(src/docs/) 
if(is_linux)
	add_subdirectory(src/linux/tests/)
endif()
add_subdirectory(src/tests/) 
add_subdirectory(examples/) 
add_subdirectory(autobahn/) 

enable_testing()

