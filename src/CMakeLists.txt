cmake_minimum_required(VERSION 3.1)
project(cio VERSION 0.0.1 LANGUAGES C)

add_library(
    target_third_party
    OBJECT
    "http-parser/http_parser.c"
    "miniz/miniz.c"
    "sha1/sha1.c"
)

set_target_properties(
    target_third_party
    PROPERTIES
        C_CLANG_TIDY ""
        COMPILE_DEFINITIONS "MINIZ_NO_STDIO;MINIZ_NO_ARCHIVE_APIS;MINIZ_NO_TIME;MINIZ_NO_ARCHIVE_WRITING_APIS;MINIZ_NO_ZLIB_APIS;MINIZ_NO_ZLIB_COMPATIBLE_NAME;MINIZ_NO_MALLOC"
)

add_library(${PROJECT_NAME}
    $<TARGET_OBJECTS:target_third_party>
    "cio_base64.c"
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	target_sources(${PROJECT_NAME} PRIVATE
        linux/cio_linux_endian.c
        linux/cio_linux_epoll.c
        linux/cio_linux_random.c
        linux/cio_linux_server_socket.c
        linux/cio_linux_socket.c
        linux/cio_linux_socket_utils.c
        linux/cio_linux_string.c
        linux/cio_linux_timer.c
    )

    target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/linux/>  
        $<INSTALL_INTERFACE:include${PROJECT_NAME}>
    )
elseif(if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows"))
    target_sources(${PROJECT_NAME} PRIVATE
        windows/cio_windows_iocp.c
        windows/cio_windows_endian.c
        windows/cio_windows_random.c
        windows/cio_windows_server_socket.c
        windows/cio_windows_socket.c
        windows/cio_windows_string.c
        windows/cio_windows_timer.c
    )

    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/windows/)
else()
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(${PROJECT_NAME}
    PROPERTIES VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}" SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
)
