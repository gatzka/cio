cmake_minimum_required(VERSION 3.3)
project(cio C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake/)
include(FindDoxygen)

include(compiler_settings)
include(generateVersion)
include(sanitizer)
include(CodeCoverage)

GenerateVersion(version ${PROJECT_NAME})
configure_file(cio_version.h.in ${CMAKE_BINARY_DIR}/generated/cio_version.h)
set(VERSION_FILE_CONFIGURED true)

enable_testing()
add_subdirectory(tests/)

set(remove_patterns "'*/tests/*' '*/http-parser/*' '*/sha1/*' '*/unity/*' '/usr/include/*'")
setup_target_for_coverage(coverage coverage ${remove_patterns})

if (DOXYGEN_FOUND)
	set(DOXYGEN_EXTRACT_ALL YES)
	set(DOXYGEN_EXTRACT_STATIC YES)
	set(DOXYGEN_GENERATE_HTML YES)
	set(DOXYGEN_GENERATE_TREEVIEW YES)
	set(DOXYGEN_HAVE_DOT NO)
	set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
	set(DOXYGEN_OUTPUT_DIRECTORY docs/)
	set(DOXYGEN_PROJECT_BRIEF "An ANSI C Conformant I/O Library.")
	set(DOXYGEN_PROJECT_NUMBER ${CIO_VERSION}${CIO_LAST})
	set(DOXYGEN_QUIET YES)
	set(DOXYGEN_RECURSIVE NO)
	set(DOXYGEN_STRICT_PROTO_MATCHING YES)
	set(DOXYGEN_STRIP_FROM_PATH ${CMAKE_BINARY_DIR}/generated/ ${CMAKE_CURRENT_SOURCE_DIR})
	set(DOXYGEN_WARN_AS_ERROR YES)
    set(DOXYGEN_PROJECT_NAME ${CMAKE_PROJECT_NAME})

	doxygen_add_docs(docs
	    ${PROJECT_SOURCE_DIR}
	    ${CMAKE_BINARY_DIR}/generated/
        COMMENT "Generating API documentation with Doxygen"
	)
else (DOXYGEN_FOUND)
  message("Doxygen need to be installed to generate the doxygen documentation")
endif (DOXYGEN_FOUND)

if(CIO_C_CLANG_TIDY)
  message(STATUS "setting clang-tidy")
  set(DO_CLANG_TIDY "${CIO_C_CLANG_TIDY}" "-extra-arg=-Wno-unknown-warning-option")
endif()

string(COMPARE EQUAL "${CMAKE_SYSTEM_NAME}" "Linux" is_linux)
string(COMPARE EQUAL "${CMAKE_SYSTEM_NAME}" "Windows" is_windows)
if(is_linux)
	add_subdirectory(linux/tests/)

    set(CIO_PLATFORM_FILES
        "linux/cio_linux_endian.c"
        "linux/cio_linux_epoll.c"
        "linux/cio_linux_random.c"
        "linux/cio_linux_server_socket.c"
        "linux/cio_linux_socket.c"
        "linux/cio_linux_socket_utils.c"
        "linux/cio_linux_string.c"
        "linux/cio_linux_timer.c"
    )
elseif(is_windows)
	set(CIO_PLATFORM_FILES
		"windows/cio_windows_iocp.c"
		"windows/cio_windows_endian.c"
		"windows/cio_windows_random.c"
		"windows/cio_windows_server_socket.c"
		"windows/cio_windows_socket.c"
		"windows/cio_windows_string.c"
		"windows/cio_windows_timer.c"
	)
endif()

add_library(target_platform
    OBJECT
    ${CIO_PLATFORM_FILES}
)
set_property(TARGET target_platform PROPERTY POSITION_INDEPENDENT_CODE 1)

set(CIO_FILES
    "cio_base64.c"
    "cio_buffered_stream.c"
    "cio_http_location.c"
    "cio_http_location_handler.c"
    "cio_http_server.c"
    "cio_utf8_checker.c"
    "cio_websocket.c"
    "cio_websocket_location_handler.c"
    "sha1/sha1.c"
)

add_library(target_platform_independent
    OBJECT
	${CIO_FILES}
)

set_property(TARGET target_platform_independent PROPERTY POSITION_INDEPENDENT_CODE 1)

add_library(
    target_third_party
    OBJECT
    "http-parser/http_parser.c"
    "http-parser/http_parser.h"
)
set_property(TARGET target_third_party PROPERTY POSITION_INDEPENDENT_CODE 1)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        if(NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 7.0.0)
            set_property(SOURCE http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wimplicit-fallthrough=0 ")
        endif()
    endif()

    set_property(SOURCE http-parser/http_parser.c  APPEND_STRING PROPERTY COMPILE_FLAGS "-Wstrict-overflow=2 -Wno-switch-default -Wno-shadow")

    foreach(_file ${CIO_PLATFORM_FILES})
        file(SHA1 ${CMAKE_CURRENT_SOURCE_DIR}/${_file} checksum)
        string(SUBSTRING ${checksum} 0 8 checksum)
        set_property(SOURCE ${_file} APPEND_STRING PROPERTY COMPILE_FLAGS "-frandom-seed=0x${checksum}")
    endforeach()

    foreach(_file ${CIO_FILES})
        file(SHA1 ${CMAKE_CURRENT_SOURCE_DIR}/${_file} checksum)
        string(SUBSTRING ${checksum} 0 8 checksum)
        set_property(SOURCE ${_file} APPEND_STRING PROPERTY COMPILE_FLAGS "-frandom-seed=0x${checksum}")
    endforeach()
endif()

set_target_properties(
    target_third_party
    PROPERTIES C_CLANG_TIDY ""
)

set_target_properties(
    target_platform_independent PROPERTIES
    C_CLANG_TIDY "${DO_CLANG_TIDY}"
)

set_target_properties(
    target_platform PROPERTIES
    C_CLANG_TIDY "${DO_CLANG_TIDY}"
)

target_include_directories(target_platform_independent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)
target_include_directories(target_platform_independent PUBLIC ${CMAKE_BINARY_DIR}/generated/)

target_include_directories(target_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)

add_library(cio_static STATIC
	$<TARGET_OBJECTS:target_platform_independent>
	$<TARGET_OBJECTS:target_platform>
	$<TARGET_OBJECTS:target_third_party>
)
target_include_directories(cio_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)
target_include_directories(cio_static PUBLIC ${CMAKE_BINARY_DIR}/generated/)

add_library(cio SHARED
   $<TARGET_OBJECTS:target_platform_independent>
   $<TARGET_OBJECTS:target_platform>
   $<TARGET_OBJECTS:target_third_party>
)
target_include_directories(cio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)
target_include_directories(cio PUBLIC ${CMAKE_BINARY_DIR}/generated/)

if(is_linux)
    target_include_directories(target_platform_independent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux/)
    target_include_directories(target_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux/)
	
	target_include_directories(cio_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux/)
	target_link_libraries(cio_static bsd)
	
	target_include_directories(cio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux/)
	target_link_libraries(cio bsd)
elseif(is_windows)
    target_include_directories(target_platform_independent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/windows/)
    target_include_directories(target_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/windows/)
	
	target_include_directories(cio_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/windows/)
	target_link_libraries(cio_static bcrypt ws2_32)

	target_include_directories(cio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/windows/)
	target_link_libraries(cio bcrypt ws2_32)
endif()

set(LIB_HEADERS
	"cio_base64.h"
	"cio_buffered_stream.h"
	"cio_compiler.h"
	"cio_endian.h"
	"cio_error_code.h"
	"cio_eventloop.h"
	"cio_http_client.h"
	"cio_http_location.h"
	"cio_http_location_handler.h"
	"cio_http_method.h"
	"cio_http_server.h"
	"cio_http_status_code.h"
	"cio_io_stream.h"
	"cio_random.h"
	"cio_read_buffer.h"
	"cio_server_socket.h"
	"cio_socket.h"
	"cio_string.h"
	"cio_timer.h"
	"cio_utf8_checker.h"
	"cio_util.h"
	"cio_websocket.h"
	"cio_websocket_location_handler.h"
	"cio_websocket_masking.h"
	"cio_write_buffer.h"
    "http-parser/http_parser.h"
    "sha1/sha1.h"
)

set(LIB_HEADERS_LINUX
    "linux/cio_error_code_impl.h"
    "linux/cio_eventloop_impl.h"
    "linux/cio_linux_socket.h"
    "linux/cio_linux_socket_utils.h"
)

set_target_properties(cio
    PROPERTIES VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}" SOVERSION ${PROJECT_VERSION_MAJOR}
)


if(is_linux)
    install(
        TARGETS cio_static cio
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
    
    install(
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/${LIB_HEADERS} DESTINATION include/cio/
    )
    install(
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/${LIB_HEADERS_LINUX} DESTINATION include/cio/linux/
    )
    install(
        FILES ${CMAKE_BINARY_DIR}/generated/cio_version.h DESTINATION include/cio/
    )
endif()

