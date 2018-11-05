cmake_minimum_required(VERSION 3.7)
project(cio_unit_test C)
enable_testing()

find_program(LCOV_PATH lcov)
find_program(GENHTML_PATH genhtml)

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


if(LCOV_PATH AND GENHTML_PATH)
    add_test(NAME clean_coverage COMMAND ${CMAKE_COMMAND} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_LIST_DIR}/reset_coverage.cmake)
    add_test(NAME collect_coverage COMMAND ${CMAKE_COMMAND} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_LIST_DIR}/collect_coverage.cmake)

    set_tests_properties(clean_coverage PROPERTIES FIXTURES_SETUP coverage)
    set_tests_properties(
        cio.test_base64
        cio.test_buffered_stream
        cio.test_http_server
        cio.test_http_server_iostream
        cio.test_read_buffer
        cio.test_utf8_checker
        cio.test_websocket
        cio.test_websocket_client
        cio.test_websocket_location_handler
        cio.test_websocket_mask
        cio.test_write_buffer
        PROPERTIES FIXTURES_REQUIRED coverage
    )
    set_tests_properties(collect_coverage PROPERTIES FIXTURES_CLEANUP coverage)

endif()


if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_subdirectory(linux)
endif()


