cmake_minimum_required(VERSION 3.0)
project(cio_unit_test C)
enable_testing()

add_library(fff INTERFACE)
target_include_directories(fff INTERFACE fff)

add_library(unity
    unity/src/unity.c
)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_LIST_DIR}/unity/src/)


add_executable(cio_test_base64 test_cio_base64.c)
target_link_libraries(cio_test_base64 fff unity cio:cio)
add_test(NAME cio.test_base64 COMMAND cio_test_base64)

add_executable(cio_test_buffered_stream test_cio_buffered_stream.c)
target_link_libraries(cio_test_buffered_stream fff unity cio:cio)
add_test(NAME cio.test_buffered_stream COMMAND cio_test_buffered_stream)

add_executable(cio_test_http_server test_cio_http_server.c)
target_link_libraries(cio_test_http_server fff unity cio:cio)
add_test(NAME cio.test_http_server COMMAND cio_test_http_server)

add_executable(cio_test_http_server_iostream test_cio_http_server_iostream.c)
target_link_libraries(cio_test_http_server_iostream fff unity cio:cio)
add_test(NAME cio.test_http_server_iostream COMMAND cio_test_http_server_iostream)

add_executable(cio_test_read_buffer test_cio_read_buffer.c)
target_link_libraries(cio_test_read_buffer fff unity cio:cio)
add_test(NAME cio.test_read_buffer COMMAND cio_test_read_buffer)

add_executable(cio_test_utf8_checker test_cio_utf8_checker.c)
target_link_libraries(cio_test_utf8_checker fff unity cio:cio)
add_test(NAME cio.test_utf8_checker COMMAND cio_test_utf8_checker)

add_executable(cio_test_websocket test_cio_websocket.c)
target_link_libraries(cio_test_websocket fff unity cio:cio)
add_test(NAME cio.test_websocket COMMAND cio_test_websocket)

add_executable(cio_test_websocket_client test_cio_websocket_client.c)
target_link_libraries(cio_test_websocket_client fff unity cio:cio)
add_test(NAME cio.test_websocket_client COMMAND cio_test_websocket_client)

add_executable(cio_test_websocket_location_handler test_cio_websocket_location_handler.c)
target_link_libraries(cio_test_websocket_location_handler fff unity cio:cio)
add_test(NAME cio.test_websocket_location_handler COMMAND cio_test_websocket_location_handler)

add_executable(cio_test_websocket_mask test_cio_websocket_mask.c)
target_link_libraries(cio_test_websocket_mask fff unity cio:cio)
add_test(NAME cio.test_websocket_mask COMMAND cio_test_websocket_mask)

add_executable(cio_test_write_buffer test_cio_write_buffer.c)
target_link_libraries(cio_test_write_buffer fff unity cio:cio)
add_test(NAME cio.test_write_buffer COMMAND cio_test_write_buffer)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_subdirectory(linux)
endif()
