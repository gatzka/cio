cmake_minimum_required(VERSION 3.1)
project(cio VERSION 0.0.1 LANGUAGES C)

find_package(Git REQUIRED)

if(NOT GIT_FOUND)
  message(FATAL_ERROR "git is required to generate version information!")
endif()

add_library(${PROJECT_NAME}
    "cio_buffered_stream.c"
    "cio_version.c"
)

add_library(${PROJECT_NAME}:${PROJECT_NAME} ALIAS ${PROJECT_NAME})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../cmake")
include(generateVersion)
GenerateVersion()
configure_file(cio_version_private.h.in ${CMAKE_BINARY_DIR}/generated/cio_version_private.h @ONLY)

include(CMakeDependentOption)

option(CIO_CONFIG_HTTP "Add HTTP support to the library " ON)
cmake_dependent_option(CIO_CONFIG_WEBSOCKETS "Add Websocket support to the library" ON "CIO_CONFIG_HTTP" OFF)
cmake_dependent_option(CIO_CONFIG_WEBSOCKET_COMPRESSION "Add Websocket compression support to the library" ON "CIO_CONFIG_WEBSOCKETS" OFF)

if(CIO_CONFIG_HTTP)
    target_sources(${PROJECT_NAME} PRIVATE
        http-parser/http_parser.c
        cio_http_location.c
        cio_http_location_handler.c
        cio_http_server.c
    )
endif()

if(CIO_CONFIG_WEBSOCKETS)
    target_sources(${PROJECT_NAME} PRIVATE
        sha1/sha1.c
        cio_utf8_checker.c
        cio_websocket.c
        cio_websocket_location_handler.c
    ) 
endif()

if(CIO_CONFIG_WEBSOCKET_COMPRESSION)
    target_sources(${PROJECT_NAME} PRIVATE miniz/miniz.c)
    set_property(SOURCE miniz/miniz.c APPENDSTRING PROPERTY COMPILE_DEFINITIONS
        "MINIZ_NO_STDIO;MINIZ_NO_ARCHIVE_APIS;MINIZ_NO_TIME;MINIZ_NO_ARCHIVE_WRITING_APIS;MINIZ_NO_ZLIB_APIS;MINIZ_NO_ZLIB_COMPATIBLE_NAME;MINIZ_NO_MALLOC"
    )
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated/>
    $<INSTALL_INTERFACE:include>
	$<$<PLATFORM_ID:Linux>:$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/linux/>>
	$<$<PLATFORM_ID:Windows>:$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/windows/>>
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

else()
    message(FATAL_ERROR "Unsupported operating system!")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_STANDARD 11
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
