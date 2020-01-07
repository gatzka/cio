cmake_minimum_required(VERSION 3.8)
include(CheckLibraryExists)
include(linux/defaults.cmake)

check_library_exists(bsd arc4random "/lib;/usr/lib;/usr/local/lib" HAVE_BSD_LIB)

if (NOT HAVE_BSD_LIB)
	message(WARNING "libbsd not found! Using unsafe random number generator!")
endif()

configure_file(linux/os_config.h.in ${PROJECT_BINARY_DIR}/generated/os_config.h)

set(CIO_PLATFORM_SOURCES
    linux/cio_linux_endian.c
    linux/cio_linux_epoll.c
    linux/cio_linux_server_socket.c
    linux/cio_linux_socket.c
    linux/cio_linux_socket_utils.c
    linux/cio_linux_string.c
    linux/cio_linux_timer.c
)

target_sources(${PROJECT_NAME}
	PRIVATE ${CIO_PLATFORM_SOURCES}
	PRIVATE $<$<BOOL:${HAVE_BSD_LIB}>:linux/cio_linux_bsd_random.c>
	PRIVATE $<$<NOT:$<BOOL:${HAVE_BSD_LIB}>>:linux/cio_linux_random.c>
)

set_property(SOURCE ${CIO_PLATFORM_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS _GNU_SOURCE)

set_property(TARGET ${PROJECT_NAME} APPEND
    PROPERTY PUBLIC_HEADER "linux/cio_error_code_impl.h;linux/cio_eventloop_impl.h;linux/cio_linux_socket.h;linux/cio_linux_socket_utils.h;linux/cio_server_socket_impl.h;linux/cio_socket_impl.h"
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
)

target_link_libraries(${PROJECT_NAME} $<$<BOOL:${HAVE_BSD_LIB}>:bsd>)
