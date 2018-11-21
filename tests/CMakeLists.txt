cmake_minimum_required(VERSION 3.7)
project(cio_unit_test C)
enable_testing()

add_library(fff INTERFACE)
target_include_directories(fff INTERFACE fff)

add_library(unity
    unity/src/unity.c
)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_LIST_DIR}/unity/src/)


add_executable(cio_test_base64 test_cio_base64.c)
add_test(NAME cio.test_base64 COMMAND cio_test_base64)

add_executable(cio_test_buffered_stream test_cio_buffered_stream.c)
add_test(NAME cio.test_buffered_stream COMMAND cio_test_buffered_stream)

add_executable(cio_test_http_server test_cio_http_server.c)
add_test(NAME cio.test_http_server COMMAND cio_test_http_server)

add_executable(cio_test_http_server_iostream test_cio_http_server_iostream.c)
add_test(NAME cio.test_http_server_iostream COMMAND cio_test_http_server_iostream)

add_executable(cio_test_read_buffer test_cio_read_buffer.c)
add_test(NAME cio.test_read_buffer COMMAND cio_test_read_buffer)

add_executable(cio_test_utf8_checker test_cio_utf8_checker.c)
add_test(NAME cio.test_utf8_checker COMMAND cio_test_utf8_checker)

add_executable(cio_test_websocket test_cio_websocket.c)
add_test(NAME cio.test_websocket COMMAND cio_test_websocket)

add_executable(cio_test_websocket_client test_cio_websocket_client.c)
add_test(NAME cio.test_websocket_client COMMAND cio_test_websocket_client)

add_executable(cio_test_websocket_location_handler test_cio_websocket_location_handler.c)
add_test(NAME cio.test_websocket_location_handler COMMAND cio_test_websocket_location_handler)

add_executable(cio_test_websocket_mask test_cio_websocket_mask.c)
add_test(NAME cio.test_websocket_mask COMMAND cio_test_websocket_mask)

add_executable(cio_test_write_buffer test_cio_write_buffer.c)
add_test(NAME cio.test_write_buffer COMMAND cio_test_write_buffer)

add_executable(cio_test_version test_cio_version.c)
add_test(NAME cio.test_version COMMAND cio_test_version)

get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    target_compile_options(${tgt} INTERFACE $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wall;-Wextra;-Wshadow;-Winit-self;-Wcast-qual;-Wcast-align;-Wformat=2;-Wwrite-strings;-Wmissing-prototypes;-Wstrict-prototypes;-Wold-style-definition;-Wstrict-overflow=5;-Wdisabled-optimization;-Wmissing-include-dirs;-Wswitch-default;-Wundef;-Wconversion;-pedantic;-pedantic-errors>)
    target_compile_options(${tgt} INTERFACE $<$<AND:$<C_COMPILER_ID:GNU>,$<VERSION_GREATER:$<C_COMPILER_VERSION>,7.0.0>>:-Wformat-truncation -Wstringop-overflow -Wformat-overflow=2>)
    target_compile_options(${tgt} INTERFACE $<$<AND:$<C_COMPILER_ID:GNU>,$<VERSION_GREATER:$<C_COMPILER_VERSION>,8.0.0>>:-Wstringop-truncation>)
    
    target_compile_options(${tgt} INTERFACE $<$<C_COMPILER_ID:Clang>:-Wdocumentation>)
	get_target_property(target_type ${tgt} TYPE)
	if (target_type STREQUAL "EXECUTABLE")
		target_link_libraries(${tgt} fff unity cio:cio)
	endif ()
endforeach()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_subdirectory(linux)
endif()


