cmake_minimum_required(VERSION 3.9)
project(cio VERSION 0.0.1 LANGUAGES C)

find_package(Git QUIET REQUIRED)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/../.git")
# Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

if(NOT CIO_BUILD_FOR_ZEPHYR)
    include(CheckTypeSize)
    CHECK_TYPE_SIZE(uint_fast32_t UINT_FAST32_T LANGUAGE C)
    if(NOT ${HAVE_FAST32_T})
        message(FATAL_ERROR "Type uint_fast32_t is not available!")
    endif()
    math(EXPR MOD_UINT_FAST32_T "${UINT_FAST32_T} % 4")
    if(NOT MOD_UINT_FAST32_T EQUAL 0)
        message(FATAL_ERROR "Size of type uint_fast32_t is not a multiple of 4!")
    endif()
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
    "cio_base64.h"
    "cio_buffered_stream.c"
    "cio_buffered_stream.h"
    "cio_compiler.h"
    "cio_endian.h"
    "cio_error_code.h"
    "cio_eventloop.h"
    "cio_inet4_socket_address.h"
    "cio_inet6_socket_address.h"
    "cio_inet_address.h"
    "cio_io_stream.h"
    "cio_random.c"
    "cio_random.h"
    "cio_read_buffer.h"
    "cio_server_socket.h"
    "cio_socket.h"
    "cio_socket_address.h"
    "cio_address_family.h"
    "cio_string.h"
    "cio_timer.h"
    "cio_util.h"
    "cio_version.c"
    "cio_version.h"
    "cio_write_buffer.h"
)
target_link_libraries(${PROJECT_NAME} PUBLIC PLATFORM)

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

add_dependencies(${PROJECT_NAME} generate_semver)

include(CMakeDependentOption)

option(CIO_CONFIG_HTTP "Add HTTP support to the library " ON)
cmake_dependent_option(CIO_CONFIG_WEBSOCKETS "Add Websocket support to the library" ON "CIO_CONFIG_HTTP" OFF)
cmake_dependent_option(CIO_CONFIG_WEBSOCKET_COMPRESSION "Add Websocket compression support to the library" OFF "CIO_CONFIG_WEBSOCKETS" OFF)

if(CIO_CONFIG_HTTP)
    add_library(http-parser OBJECT
        http-parser/http_parser.c
    )
    set_target_properties(http-parser PROPERTIES
        COMPILE_OPTIONS "$<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wno-shadow;-Wno-sign-conversion;-Wno-conversion;-Wno-switch-default>"
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
        cio_http_client.h
        cio_http_location.h
        cio_http_location_handler.h
        cio_http_method.h
        cio_http_server.h
        cio_http_status_code.h
    )

    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY PUBLIC_HEADER 
        "cio_http_client.h"
        "cio_http_location.h"
        "cio_http_location_handler.h"
        "cio_http_method.h"
        "cio_http_server.h"
        "cio_http_status_code.h"
    )
endif()

if(CIO_CONFIG_WEBSOCKETS)
    add_library(sha OBJECT
        sha1/sha1.c
    )
    if(NOT CIO_BUILD_FOR_ZEPHYR)
        target_compile_features(sha PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:c_std_99>)
    endif()

    set_target_properties(sha PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    	C_STANDARD_REQUIRED ON
    	C_EXTENSIONS OFF
        C_VISIBILITY_PRESET hidden
        C_CLANG_TIDY ""
    )
    target_sources(${PROJECT_NAME} PRIVATE
        $<TARGET_OBJECTS:sha>
        cio_utf8_checker.h
        cio_base64.c
        cio_base64.h
        cio_utf8_checker.c
        cio_websocket.c
        cio_websocket.h
        cio_websocket_location_handler.c
        cio_websocket_location_handler.h
        cio_websocket_masking.h
    ) 

    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY PUBLIC_HEADER
        "cio_base64.h"
        "cio_utf8_checker.h"
        "cio_websocket.h"
        "cio_websocket_location_handler.h"
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
    $<BUILD_INTERFACE:$<$<PLATFORM_ID:Linux>:${CMAKE_CURRENT_LIST_DIR}/linux/>>
    $<BUILD_INTERFACE:$<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_LIST_DIR}/windows/>>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/>
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_subdirectory(linux)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_subdirectory(windows)
elseif(${CIO_BUILD_FOR_ZEPHYR})
    include(zephyr/CMakeLists.txt)
endif()

if(NOT CIO_BUILD_FOR_ZEPHYR)
    target_compile_features(${PROJECT_NAME} PUBLIC $<$<NOT:$<C_COMPILER_ID:MSVC>>:c_std_11>)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
)

set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY PUBLIC_HEADER
    "cio_address_family.h"
    "cio_buffered_stream.h"
    "cio_compiler.h"
    "cio_endian.h"
    "cio_error_code.h"
    "cio_eventloop.h"
    "cio_inet4_socket_address.h"
    "cio_inet6_socket_address.h"
    "cio_inet_address.h"
    "cio_io_stream.h"
    "cio_random.h"
    "cio_read_buffer.h"
    "cio_server_socket.h"
    "cio_socket.h"
    "cio_socket_address.h"
    "cio_string.h"
    "cio_timer.h"
    "cio_util.h"
    "cio_version.h"
    "cio_write_buffer.h"
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY PUBLIC_HEADER
        "linux/cio_unix_address.h"
    )
endif()

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME} EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/generated/${PROJECT_NAME}_export.h)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} PLATFORM
    EXPORT ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file("${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${${PROJECT_NAME}_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION lib/cmake/${PROJECT_NAME}
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/${PROJECT_NAME}_export.h"
    DESTINATION include/cio/
)

if(NOT CIO_BUILD_FOR_ZEPHYR)
    install(EXPORT ${PROJECT_NAME}
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION lib/cmake/${PROJECT_NAME}
        FILE ${PROJECT_NAME}Config.cmake
    )
endif()

include(FindDoxygen)

if(DOXYGEN_FOUND)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
	set(DOXYGEN_HAVE_DOT NO)
    set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
    set(DOXYGEN_OUTPUT_DIRECTORY docs/)
    set(DOXYGEN_PROJECT_BRIEF "An ANSI C Conformant I/O Library.")
	set(DOXYGEN_PROJECT_NUMBER ${${PROJECT_NAME}_VERSION})
    set(DOXYGEN_QUIET YES)
    set(DOXYGEN_RECURSIVE NO)
    set(DOXYGEN_STRICT_PROTO_MATCHING YES)
    set(DOXYGEN_STRIP_FROM_PATH ${CMAKE_CURRENT_BINARY_DIR}/generated/ ${CMAKE_CURRENT_SOURCE_DIR})
    set(DOXYGEN_WARN_AS_ERROR YES)
    set(DOXYGEN_PROJECT_NAME ${CMAKE_PROJECT_NAME})
    set(DOXYGEN_EXCLUDE_PATTERNS "*private*;*.c;cio_websocket_masking.h")

    doxygen_add_docs(docs
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/generated/
        COMMENT "Generating API documentation with Doxygen"
    )
else(DOXYGEN_FOUND)
    message(STATUS "Doxygen need to be installed to generate the doxygen documentation")
endif(DOXYGEN_FOUND)

get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
	if(CIO_ENABLE_LTO)
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endforeach()

