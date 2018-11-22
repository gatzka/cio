cmake_minimum_required(VERSION 3.8)
project(cio VERSION 0.0.1 LANGUAGES C CXX)

include(CMakePackageConfigHelpers)

include(CheckTypeSize)
CHECK_TYPE_SIZE(uint_fast32_t fast32_t LANGUAGE C)
if (NOT ${HAVE_fast32_t})
    message(FATAL_ERROR "Type uint_fast32_t is not available!")
endif()
math(EXPR mod_uint_fast32_t "${fast32_t} % 4")
if (NOT mod_uint_fast32_t EQUAL 0)
    message(FATAL_ERROR "Size of type uint_fast32_t is not a multiple of 4!")
endif()

add_custom_target(generate_semver
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_NAME=${PROJECT_NAME}
        -DPROJECT_VERSION_MAJOR=${${PROJECT_NAME}_VERSION_MAJOR}
        -DPROJECT_VERSION_MINOR=${${PROJECT_NAME}_VERSION_MINOR}
        -DPROJECT_VERSION_PATCH=${${PROJECT_NAME}_VERSION_PATCH}
        -DGIT_WORK_DIR=${CMAKE_CURRENT_LIST_DIR}
        -DINPUT_FILE=${CMAKE_CURRENT_LIST_DIR}/cio_version_private.h.in
        -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/generated/cio_version_private.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_version.cmake
)

add_library(${PROJECT_NAME}
    "cio_buffered_stream.c"
    "cio_version.c"
)

target_compile_options(${PROJECT_NAME} PRIVATE $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wall;-Wextra;-Wshadow;-Winit-self;-Wcast-qual;-Wcast-align;-Wformat=2;-Wwrite-strings;-Wmissing-prototypes;-Wstrict-prototypes;-Wold-style-definition;-Wstrict-overflow=5;-Wdisabled-optimization;-Wmissing-include-dirs;-Wswitch-default;-Wundef;-Wconversion;-pedantic;-pedantic-errors>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<AND:$<C_COMPILER_ID:GNU>,$<VERSION_GREATER:$<C_COMPILER_VERSION>,7.0.0>>:-Wformat-truncation -Wstringop-overflow -Wformat-overflow=2>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<AND:$<C_COMPILER_ID:GNU>,$<VERSION_GREATER:$<C_COMPILER_VERSION>,8.0.0>>:-Wstringop-truncation>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang>:-Wdocumentation>)
    
add_library(${PROJECT_NAME}:${PROJECT_NAME} ALIAS ${PROJECT_NAME})

add_dependencies(${PROJECT_NAME} generate_semver)

include(CMakeDependentOption)

option(CIO_CONFIG_HTTP "Add HTTP support to the library " ON)
cmake_dependent_option(CIO_CONFIG_WEBSOCKETS "Add Websocket support to the library" ON "CIO_CONFIG_HTTP" OFF)
cmake_dependent_option(CIO_CONFIG_WEBSOCKET_COMPRESSION "Add Websocket compression support to the library" ON "CIO_CONFIG_WEBSOCKETS" OFF)

if(CIO_CONFIG_HTTP)
    add_library(http-parser OBJECT
        http-parser/http_parser.c
    )
    set_target_properties(http-parser PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
		C_CLANG_TIDY ""
    )
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/http-parser/http_parser.h"
        DESTINATION include/cio/http-parser/
    )
    target_sources(${PROJECT_NAME} PRIVATE
        $<TARGET_OBJECTS:http-parser>
        cio_http_location.c
        cio_http_location_handler.c
        cio_http_server.c
    )

    set_property(TARGET ${PROJECT_NAME}
        APPEND
        PROPERTY PUBLIC_HEADER "cio_http_client.h;cio_http_location.h;cio_http_location_handler.h;cio_http_method.h;cio_http_server.h;cio_http_status_code.h"
    )
endif()

if(CIO_CONFIG_WEBSOCKETS)
    add_library(sha OBJECT
        sha1/sha1.c
    )
    target_compile_features(sha PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:c_std_99>)

    set_target_properties(sha PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
		C_CLANG_TIDY ""
    )
    target_sources(${PROJECT_NAME} PRIVATE
        $<TARGET_OBJECTS:sha>
        cio_base64.c
        cio_utf8_checker.c
        cio_websocket.c
        cio_websocket_location_handler.c
    ) 

    set_property(TARGET ${PROJECT_NAME}
        APPEND
        PROPERTY PUBLIC_HEADER "cio_base64.h;cio_utf8_checker.h;cio_websocket.h;cio_websocket_location_handler.h"
    )

endif()

if(CIO_CONFIG_WEBSOCKET_COMPRESSION)
    add_library(miniz OBJECT
        miniz/miniz.c
    )
    set_target_properties(miniz PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
		C_CLANG_TIDY ""
    )
    target_compile_definitions(miniz PRIVATE MINIZ_NO_STDIO MINIZ_NO_ARCHIVE_APIS MINIZ_NO_TIME MINIZ_NO_ARCHIVE_WRITING_APIS MINIZ_NO_ZLIB_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAME MINIZ_NO_MALLOC)
    target_sources(${PROJECT_NAME} PRIVATE $<TARGET_OBJECTS:miniz>)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/>
    $<INSTALL_INTERFACE:include/cio/>
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    include(linux/CMakeLists.txt)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    include(windows/CMakeLists.txt)
endif()

target_compile_features(${PROJECT_NAME} PUBLIC $<$<NOT:$<C_COMPILER_ID:MSVC>>:c_std_11>)
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
)

set_property(TARGET ${PROJECT_NAME}
    APPEND
    PROPERTY PUBLIC_HEADER "cio_buffered_stream.h;cio_compiler.h;cio_endian.h;cio_error_code.h;cio_eventloop.h;cio_io_stream.h;cio_random.h;cio_read_buffer.h;cio_server_socket.h;cio_socket.h;cio_string.h;cio_timer.h;cio_util.h;cio_version.h;cio_write_buffer.h"
)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME} EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/generated/${PROJECT_NAME}_export.h)

# get_target_property(cio_sources cio SOURCES)
# foreach(CIOSRC IN LISTS cio_sources)
#   if(EXISTS  ${CMAKE_CURRENT_LIST_DIR}/${CIOSRC})
#       message(STATUS "----------------- ${CIOSRC}")
#   else()
#       message(STATUS "+++++++++++++++++ ${CIOSRC}")
#   endif()
# endforeach()

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include/cio
)

write_basic_package_version_file("${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${${PROJECT_NAME}_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES "${CMAKE_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION lib/cmake/${PROJECT_NAME}
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/${PROJECT_NAME}_export.h"
    DESTINATION include/cio/
)

install(EXPORT ${PROJECT_NAME}
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
    FILE ${PROJECT_NAME}Config.cmake
)

include(FindDoxygen)

if (DOXYGEN_FOUND)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_HAVE_DOT NO)
    set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
    set(DOXYGEN_OUTPUT_DIRECTORY docs/)
    set(DOXYGEN_PROJECT_BRIEF "An ANSI C Conformant I/O Library.")
    set(DOXYGEN_PROJECT_NUMBER ${CIO_VERSION})
    set(DOXYGEN_QUIET YES)
    set(DOXYGEN_RECURSIVE NO)
    set(DOXYGEN_STRICT_PROTO_MATCHING YES)
    set(DOXYGEN_STRIP_FROM_PATH ${CMAKE_CURRENT_BINARY_DIR}/generated/ ${CMAKE_CURRENT_SOURCE_DIR})
    set(DOXYGEN_WARN_AS_ERROR YES)
    set(DOXYGEN_PROJECT_NAME ${CMAKE_PROJECT_NAME})
    set(DOXYGEN_EXCLUDE_PATTERNS "*private*;*.c")

    doxygen_add_docs(docs
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/generated/
        COMMENT "Generating API documentation with Doxygen"
    )
else (DOXYGEN_FOUND)
    message(STATUS "Doxygen need to be installed to generate the doxygen documentation")
endif (DOXYGEN_FOUND)
