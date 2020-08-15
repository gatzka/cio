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

add_custom_target(${PROJECT_NAME}_generate_semver
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_NAME=${PROJECT_NAME}
        -DPROJECT_VERSION_MAJOR=${${PROJECT_NAME}_VERSION_MAJOR}
        -DPROJECT_VERSION_MINOR=${${PROJECT_NAME}_VERSION_MINOR}
        -DPROJECT_VERSION_PATCH=${${PROJECT_NAME}_VERSION_PATCH}
        -DGIT_WORK_DIR=${CMAKE_CURRENT_LIST_DIR}
        -DINPUT_FILE=${CMAKE_CURRENT_LIST_DIR}/include/cio/cio_version_private.h.in
        -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/generated/cio/cio_version_private.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_version.cmake
)

add_library(${PROJECT_NAME}
    include/cio/cio_address_family.h
    include/cio/cio_base64.h
    include/cio/cio_buffered_stream.h
    include/cio/cio_compiler.h
    include/cio/cio_endian.h
    include/cio/cio_error_code.h
    include/cio/cio_eventloop.h
    include/cio/cio_inet4_socket_address.h
    include/cio/cio_inet6_socket_address.h
    include/cio/cio_inet_address.h
    include/cio/cio_io_stream.h
    include/cio/cio_random.h
    include/cio/cio_read_buffer.h
    include/cio/cio_server_socket.h
    include/cio/cio_socket.h
    include/cio/cio_socket_address.h
    include/cio/cio_string.h
    include/cio/cio_timer.h
    include/cio/cio_uart.h
    include/cio/cio_util.h
    include/cio/cio_version.h
    include/cio/cio_write_buffer.h
    src/cio_buffered_stream.c
    src/cio_random.c
    src/cio_version.c
)

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_generate_semver)

include(CMakeDependentOption)

option(CIO_CONFIG_HTTP "Add HTTP support to the library " ON)
cmake_dependent_option(CIO_CONFIG_WEBSOCKETS "Add Websocket support to the library" ON "CIO_CONFIG_HTTP" OFF)
cmake_dependent_option(CIO_CONFIG_WEBSOCKET_COMPRESSION "Add Websocket compression support to the library" OFF "CIO_CONFIG_WEBSOCKETS" OFF)

if(CIO_CONFIG_HTTP)
    add_library(http_parser OBJECT
        cio/http-parser/http_parser.c
        cio/http-parser/http_parser.h
    )
    set_target_properties(http_parser PROPERTIES
        COMPILE_OPTIONS "$<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wno-shadow;-Wno-sign-conversion;-Wno-conversion;-Wno-switch-default>"
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
        C_CLANG_TIDY ""
    )
    if(CIO_BUILD_FOR_ZEPHYR)
        target_link_libraries(http_parser PRIVATE zephyr)
    endif()

    install(FILES "${CMAKE_CURRENT_LIST_DIR}/cio/http-parser/http_parser.h"
        DESTINATION include/cio/http-parser/
    )
    target_sources(${PROJECT_NAME} PRIVATE
        $<TARGET_OBJECTS:http_parser>
        include/cio/cio_http_client.h
        include/cio/cio_http_location.h
        include/cio/cio_http_location_handler.h
        include/cio/cio_http_method.h
        include/cio/cio_http_server.h
        include/cio/cio_http_status_code.h
        src/cio_http_location.c
        src/cio_http_location_handler.c
        src/cio_http_server.c
    )

    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY PUBLIC_HEADER 
        include/cio/cio_http_client.h
        include/cio/cio_http_location.h
        include/cio/cio_http_location_handler.h
        include/cio/cio_http_method.h
        include/cio/cio_http_server.h
        include/cio/cio_http_status_code.h
    )

    target_include_directories(${PROJECT_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    )
endif()

if(CIO_CONFIG_WEBSOCKETS)
    add_library(sha OBJECT
        cio/sha1/sha1.c
    )
    set_target_properties(sha PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        C_VISIBILITY_PRESET hidden
        C_CLANG_TIDY ""
    )
    if(NOT CIO_BUILD_FOR_ZEPHYR)
        if(NOT CMAKE_C_COMPILER_ID STREQUAL "MSVC")
            set_target_properties(sha PROPERTIES
                C_STANDARD 99 
            )
        endif()
    else()
        target_link_libraries(sha PRIVATE zephyr)
    endif()

    target_sources(${PROJECT_NAME} PRIVATE
        $<TARGET_OBJECTS:sha>
        include/cio/cio_base64.h
        include/cio/cio_utf8_checker.h
        include/cio/cio_websocket.h
        include/cio/cio_websocket_location_handler.h
        include/cio/cio_websocket_masking.h
        src/cio_base64.c
        src/cio_utf8_checker.c
        src/cio_websocket.c
        src/cio_websocket_location_handler.c
    ) 

    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY PUBLIC_HEADER
        include/cio/cio_base64.h
        include/cio/cio_utf8_checker.h
        include/cio/cio_websocket.h
        include/cio/cio_websocket_location_handler.h
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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include/>
    $<BUILD_INTERFACE:$<$<PLATFORM_ID:Linux>:${CMAKE_CURRENT_LIST_DIR}/include/platform/linux/>>
    $<BUILD_INTERFACE:$<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_LIST_DIR}/include/platform/windows/>>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/>
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    include(src/platform/linux/defaults.cmake)
    configure_file(src/platform/linux/cio_os_config.h.in ${PROJECT_BINARY_DIR}/generated/cio/cio_os_config.h)

    target_sources(${PROJECT_NAME} PRIVATE
        src/platform/linux/cio_linux_endian.c
        src/platform/linux/cio_linux_epoll.c
        src/platform/linux/cio_linux_random.c
        src/platform/linux/cio_linux_server_socket.c
        src/platform/linux/cio_linux_socket.c
        src/platform/linux/cio_linux_socket_utils.c
        src/platform/linux/cio_linux_string.c
        src/platform/linux/cio_linux_timer.c
        src/platform/linux/cio_linux_uart.c
        src/platform/linux/cio_unix_address.c
        src/platform/shared/cio_inet_address_impl.c
        src/platform/shared/cio_socket_address_impl.c
    )

    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY PUBLIC_HEADER
        include/platform/linux/cio/cio_address_family_impl.h
        include/platform/linux/cio/cio_error_code_impl.h
        include/platform/linux/cio/cio_eventloop_impl.h
        include/platform/linux/cio/cio_inet4_socket_address_impl.h
        include/platform/linux/cio/cio_inet6_socket_address_impl.h
        include/platform/linux/cio/cio_inet_address_impl.h
        include/platform/linux/cio/cio_server_socket_impl.h
        include/platform/linux/cio/cio_socket_address_impl.h
        include/platform/linux/cio/cio_socket_impl.h
        include/platform/linux/cio/cio_timer_impl.h
        include/platform/linux/cio/cio_uart_impl.h
        include/platform/linux/cio/cio_unix_address.h
    )
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    target_sources(${PROJECT_NAME} PRIVATE
        src/platform/shared/cio_inet_address_impl.c
        src/platform/shared/cio_socket_address_impl.c
        src/platform/shared/cio_string_memmem.c
        src/platform/windows/cio_windows_endian.c
        src/platform/windows/cio_windows_iocp.c
        src/platform/windows/cio_windows_random.c
        src/platform/windows/cio_windows_server_socket.c
        src/platform/windows/cio_windows_socket.c
        src/platform/windows/cio_windows_socket_utils.c
        src/platform/windows/cio_windows_string.c
        src/platform/windows/cio_windows_timer.c
        src/platform/windows/cio_windows_uart.c
    )

    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY PUBLIC_HEADER
        include/platform/windows/cio/cio_address_family_impl.h
        include/platform/windows/cio/cio_error_code_impl.h
        include/platform/windows/cio/cio_eventloop_impl.h
        include/platform/windows/cio/cio_inet4_socket_address_impl.h
        include/platform/windows/cio/cio_inet6_socket_address_impl.h
        include/platform/windows/cio/cio_inet_address_impl.h
        include/platform/windows/cio/cio_server_socket_impl.h
        include/platform/windows/cio/cio_socket_address_impl.h
        include/platform/windows/cio/cio_socket_impl.h
        include/platform/windows/cio/cio_timer_impl.h
        include/platform/windows/cio/cio_uart_impl.h
    )

    target_link_libraries( ${PROJECT_NAME} bcrypt ws2_32 Setupapi)

elseif(${CIO_BUILD_FOR_ZEPHYR})
    target_sources(${PROJECT_NAME} PRIVATE
        src/platform/shared/cio_inet_address_impl.c
        src/platform/shared/cio_socket_address_impl.c
        src/platform/shared/cio_string_memmem.c
        src/platform/zephyr/cio_zephyr_endian.c
        src/platform/zephyr/cio_zephyr_eventloop.c
        src/platform/zephyr/cio_zephyr_random.c
        src/platform/zephyr/cio_zephyr_server_socket.c
        src/platform/zephyr/cio_zephyr_socket.c
        src/platform/zephyr/cio_zephyr_socket_utils.c
        src/platform/zephyr/cio_zephyr_string.c
        src/platform/zephyr/cio_zephyr_timer.c
    )
endif()

if(NOT CIO_BUILD_FOR_ZEPHYR AND NOT CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        C_STANDARD 11
    )
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
)

set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY PUBLIC_HEADER
    include/cio/cio_address_family.h
    include/cio/cio_buffered_stream.h
    include/cio/cio_compiler.h
    include/cio/cio_endian.h
    include/cio/cio_error_code.h
    include/cio/cio_eventloop.h
    include/cio/cio_inet4_socket_address.h
    include/cio/cio_inet6_socket_address.h
    include/cio/cio_inet_address.h
    include/cio/cio_io_stream.h
    include/cio/cio_random.h
    include/cio/cio_read_buffer.h
    include/cio/cio_server_socket.h
    include/cio/cio_socket.h
    include/cio/cio_socket_address.h
    include/cio/cio_string.h
    include/cio/cio_timer.h
    include/cio/cio_util.h
    include/cio/cio_version.h
    include/cio/cio_write_buffer.h
)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME} EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/generated/cio/${PROJECT_NAME}_export.h)

if(NOT CIO_BUILD_FOR_ZEPHYR)
    include(GNUInstallDirs)
    install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
    )
    
    include(CMakePackageConfigHelpers)
    write_basic_package_version_file("${PROJECT_NAME}ConfigVersion.cmake"
        VERSION ${${PROJECT_NAME}_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        DESTINATION lib/cmake/${PROJECT_NAME}
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/cio/${PROJECT_NAME}_export.h"
        DESTINATION include/${PROJECT_NAME}
    )

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
    set(DOXYGEN_PROJECT_NAME ${PROJECT_NAME})
    set(DOXYGEN_EXCLUDE_PATTERNS "*private*;*.c;cio_websocket_masking.h;cio_export.h")
    set(DOXYGEN_ENABLE_PREPROCESSING YES)
    set(DOXYGEN_MACRO_EXPANSION YES)
    set(DOXYGEN_EXPAND_ONLY_PREDEF YES)
    set(DOXYGEN_PREDEFINED CIO_EXPORT)

    doxygen_add_docs(${PROJECT_NAME}_docs
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

