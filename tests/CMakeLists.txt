cmake_minimum_required(VERSION 3.7)
project(cio_unit_test C)
enable_testing()

add_library(fff INTERFACE)
target_include_directories(fff INTERFACE fff)

add_library(unity
    unity/src/unity.c
)
set_target_properties(unity PROPERTIES
    C_CLANG_TIDY ""
    COMPILE_OPTIONS "$<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wno-strict-overflow>"
)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_LIST_DIR}/unity/src/)

add_executable(test_cio_base64 test_cio_base64.c)
add_test(NAME cio.common.test_base64 COMMAND test_cio_base64)

add_executable(test_cio_buffered_stream test_cio_buffered_stream.c)
add_test(NAME cio.common.test_buffered_stream COMMAND test_cio_buffered_stream)

add_executable(test_cio_http_server test_cio_http_server.c)
add_test(NAME cio.common.test_http_server COMMAND test_cio_http_server)

add_executable(test_cio_http_server_iostream test_cio_http_server_iostream.c)
add_test(NAME cio.common.test_http_server_iostream COMMAND test_cio_http_server_iostream)

add_executable(test_cio_http_location test_cio_http_location.c)
add_test(NAME cio.common.test_http_location COMMAND test_cio_http_location)

add_executable(test_cio_inet_address test_cio_inet_address.c)
add_test(NAME cio.common.test_inet_address COMMAND test_cio_inet_address)

add_executable(test_cio_random test_cio_random.c)
add_test(NAME cio.common.test_random COMMAND test_cio_random)

add_executable(test_cio_read_buffer test_cio_read_buffer.c)
add_test(NAME cio.common.test_read_buffer COMMAND test_cio_read_buffer)
 
add_executable(test_cio_utf8_checker test_cio_utf8_checker.c)
add_test(NAME cio.common.test_utf8_checker COMMAND test_cio_utf8_checker)

add_executable(test_cio_websocket test_cio_websocket.c)
add_test(NAME cio.common.test_websocket COMMAND test_cio_websocket)

add_executable(test_cio_websocket_iostream test_cio_websocket_iostream.c)
add_test(NAME cio.common.test_websocket_iostream COMMAND test_cio_websocket_iostream)

add_executable(test_cio_websocket_location_handler test_cio_websocket_location_handler.c)
add_test(NAME cio.common.test_websocket_location_handler COMMAND test_cio_websocket_location_handler)

add_executable(test_cio_websocket_mask test_cio_websocket_mask.c)
add_test(NAME cio.common.test_websocket_mask COMMAND test_cio_websocket_mask)

add_executable(test_cio_write_buffer test_cio_write_buffer.c)
add_test(NAME cio.common.test_write_buffer COMMAND test_cio_write_buffer)

add_executable(test_cio_version test_cio_version.c)
add_test(NAME cio.common.test_version COMMAND test_cio_version)

get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
    get_target_property(target_type ${tgt} TYPE)
    if (target_type STREQUAL "EXECUTABLE")
        target_link_libraries(${tgt} fff unity cio:cio)
        set_target_properties(${tgt} PROPERTIES
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
            C_CLANG_TIDY ""
        )
    endif()
endforeach()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_subdirectory(linux)
endif()


