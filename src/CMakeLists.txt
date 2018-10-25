cmake_minimum_required(VERSION 3.1)
project(cio VERSION 0.0.1 LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CMakeDependentOption)

option(CIO_CONFIG_HTTP "Add HTTP support to the library " ON)
cmake_dependent_option(CIO_CONFIG_WEBSOCKETS "Add Websocket support to the library" ON "CIO_CONFIG_HTTP" OFF)
cmake_dependent_option(CIO_CONFIG_WEBSOCKET_COMPRESSION "Add Websocket compression support to the library" ON "CIO_CONFIG_WEBSOCKETS" OFF)

add_library(
    target_third_party
    OBJECT
    ""
)

if(CIO_CONFIG_HTTP)
    target_sources(target_third_party PRIVATE
        http-parser/http_parser.c
    )
endif()

if(CIO_CONFIG_WEBSOCKETS)
    target_sources(target_third_party PRIVATE
        sha1/sha1.c
    )
endif()

if(CIO_CONFIG_WEBSOCKET_COMPRESSION)
    target_sources(target_third_party PRIVATE
        miniz/miniz.c
    )
endif()

set_target_properties(
    target_third_party
    PROPERTIES
        C_CLANG_TIDY ""
        COMPILE_DEFINITIONS "MINIZ_NO_STDIO;MINIZ_NO_ARCHIVE_APIS;MINIZ_NO_TIME;MINIZ_NO_ARCHIVE_WRITING_APIS;MINIZ_NO_ZLIB_APIS;MINIZ_NO_ZLIB_COMPATIBLE_NAME;MINIZ_NO_MALLOC"
)

add_library(${PROJECT_NAME}
    $<TARGET_OBJECTS:target_third_party>
    "cio_buffered_stream.c"
)

add_library(${PROJECT_NAME}:${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    $<INSTALL_INTERFACE:include>
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
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/linux/>  
        $<INSTALL_INTERFACE:include${PROJECT_NAME}>
    )
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    target_sources(${PROJECT_NAME} PRIVATE
        windows/cio_windows_iocp.c
        windows/cio_windows_endian.c
        windows/cio_windows_random.c
        windows/cio_windows_server_socket.c
        windows/cio_windows_socket.c
        windows/cio_windows_string.c
        windows/cio_windows_timer.c
    )

    target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/windows/>  
        $<INSTALL_INTERFACE:include${PROJECT_NAME}>
    )
else()
    message(FATAL_ERROR "Unsupported operating system!")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)

get_target_property(cio_sources cio SOURCES)
foreach(CIOSRC IN LISTS cio_sources)
	if(EXISTS  ${CMAKE_CURRENT_LIST_DIR}/${CIOSRC})
		message(STATUS "----------------- ${CIOSRC}")
	else()
		message(STATUS "+++++++++++++++++ ${CIOSRC}")
	endif()
endforeach()


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
