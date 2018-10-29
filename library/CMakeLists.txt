cmake_minimum_required(VERSION 3.1)
project(cio VERSION 0.0.1 LANGUAGES C)

include(CMakePackageConfigHelpers)
include(CheckTypeSize)
CHECK_TYPE_SIZE(uint_fast32_t fast32_t LANGUAGE C)
if (NOT ${HAVE_fast32_t})
	message(FATAL_ERROR "Type uint_fast32_t is not available!")
endif()

if (NOT ${HAVE_fast32_t})
	message(FATAL_ERROR "Type uint_fast32_t is not available!")
endif()
math(EXPR mod_uint_fast32_t "${fast32_t} % 4")
if (NOT mod_uint_fast32_t EQUAL 0)
	message(FATAL_ERROR "Size of type uint_fast32_t is not a multiple of 4!")
endif()

add_library(${PROJECT_NAME}
    "src/cio_buffered_stream.c"
    "src/cio_version.c"
)

add_library(${PROJECT_NAME}:${PROJECT_NAME} ALIAS ${PROJECT_NAME})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../cmake")
include(GenerateSemverFromGit)
GenerateSemverInfo()
configure_file(include/cio_version_private.h.in ${CMAKE_BINARY_DIR}/generated/cio_version_private.h @ONLY)

include(CMakeDependentOption)

option(CIO_CONFIG_HTTP "Add HTTP support to the library " ON)
cmake_dependent_option(CIO_CONFIG_WEBSOCKETS "Add Websocket support to the library" ON "CIO_CONFIG_HTTP" OFF)
cmake_dependent_option(CIO_CONFIG_WEBSOCKET_COMPRESSION "Add Websocket compression support to the library" ON "CIO_CONFIG_WEBSOCKETS" OFF)

if(CIO_CONFIG_HTTP)
	add_library(http-parser OBJECT
        src/http-parser/http_parser.c
	)
	install(FILES "${CMAKE_CURRENT_LIST_DIR}/src/http-parser/http_parser.h"
		DESTINATION include/cio/http-parser/
	)
    target_sources(${PROJECT_NAME} PRIVATE
		$<TARGET_OBJECTS:http-parser>
        src/cio_http_location.c
        src/cio_http_location_handler.c
        src/cio_http_server.c
    )
endif()

if(CIO_CONFIG_WEBSOCKETS)
	add_library(sha OBJECT
        src/sha1/sha1.c
	)
    target_sources(${PROJECT_NAME} PRIVATE
		$<TARGET_OBJECTS:sha>
		src/cio_base64.c
        src/cio_utf8_checker.c
        src/cio_websocket.c
        src/cio_websocket_location_handler.c
    ) 
endif()

if(CIO_CONFIG_WEBSOCKET_COMPRESSION)
	add_library(miniz OBJECT
        src/miniz/miniz.c
	)
    target_compile_definitions(miniz PRIVATE MINIZ_NO_STDIO MINIZ_NO_ARCHIVE_APIS MINIZ_NO_TIME MINIZ_NO_ARCHIVE_WRITING_APIS MINIZ_NO_ZLIB_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAME MINIZ_NO_MALLOC)
    target_sources(${PROJECT_NAME} PRIVATE $<TARGET_OBJECTS:miniz>)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src/>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include/>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated/>
    $<INSTALL_INTERFACE:include/cio/>
	$<$<PLATFORM_ID:Linux>:$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include/linux/>>
	$<$<PLATFORM_ID:Windows>:$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include/windows/>>
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	target_sources(${PROJECT_NAME} PRIVATE
        src/linux/cio_linux_endian.c
        src/linux/cio_linux_epoll.c
        src/linux/cio_linux_random.c
        src/linux/cio_linux_server_socket.c
        src/linux/cio_linux_socket.c
        src/linux/cio_linux_socket_utils.c
        src/linux/cio_linux_string.c
        src/linux/cio_linux_timer.c
    )

	target_link_libraries(${PROJECT_NAME} bsd)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    target_sources(${PROJECT_NAME} PRIVATE
        src/windows/cio_windows_iocp.c
        src/windows/cio_windows_endian.c
        src/windows/cio_windows_random.c
        src/windows/cio_windows_server_socket.c
        src/windows/cio_windows_socket.c
        src/windows/cio_windows_string.c
        src/windows/cio_windows_timer.c
    )

	target_link_libraries(cio bcrypt ws2_32)
else()
    message(FATAL_ERROR "Unsupported operating system!")
endif()

target_compile_features(${PROJECT_NAME} PUBLIC c_std_99)
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# get_target_property(cio_sources cio SOURCES)
# foreach(CIOSRC IN LISTS cio_sources)
# 	if(EXISTS  ${CMAKE_CURRENT_LIST_DIR}/${CIOSRC})
# 		message(STATUS "----------------- ${CIOSRC}")
# 	else()
# 		message(STATUS "+++++++++++++++++ ${CIOSRC}")
# 	endif()
# endforeach()

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(
	DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include/
	DESTINATION include/cio/
	PATTERN "linux" EXCLUDE
	PATTERN "windows" EXCLUDE
)

install(
	DIRECTORY
		$<$<PLATFORM_ID:Linux>:${CMAKE_CURRENT_LIST_DIR}/include/linux/>
		$<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_LIST_DIR}/include/windows/>
	DESTINATION include/cio/
)

write_basic_package_version_file("${PROJECT_NAME}ConfigVersion.cmake"
	VERSION ${${PROJECT_NAME}_VERSION}
	COMPATIBILITY SameMajorVersion
)

install(FILES "${CMAKE_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION lib/cmake/${PROJECT_NAME}
)

install(EXPORT ${PROJECT_NAME}
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
    FILE ${PROJECT_NAME}Config.cmake
)
