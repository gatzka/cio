cmake_minimum_required(VERSION 3.9)
include(CheckLibraryExists)
include(defaults.cmake)

check_library_exists(bsd arc4random "/lib;/usr/lib;/usr/local/lib" HAVE_BSD_LIB)

if(NOT HAVE_BSD_LIB)
	message(WARNING "libbsd not found! Using unsafe random number generator!")
endif()

configure_file(cio_os_config.h.in ${PROJECT_BINARY_DIR}/generated/cio_os_config.h)

add_library(PLATFORM OBJECT
	cio_socket_address_impl.c
	cio_socket_address_impl.h
	cio_socket_impl.h
    cio_error_code_impl.h
    cio_eventloop_impl.h
    cio_inet4_socket_address_impl.h
    cio_inet6_socket_address_impl.h
    cio_linux_endian.c
    cio_linux_epoll.c
    cio_linux_server_socket.c
    cio_linux_socket.c
    cio_linux_socket.h
    cio_linux_socket_utils.c
    cio_linux_socket_utils.h
    cio_linux_string.c
    cio_linux_timer.c
    cio_os_config.h.in
    cio_server_socket_impl.h
    cio_timer_impl.h
    cio_unix_address.h
)

target_sources(PLATFORM
    PRIVATE $<$<BOOL:${HAVE_BSD_LIB}>:cio_linux_bsd_random.c>
    PRIVATE $<$<NOT:$<BOOL:${HAVE_BSD_LIB}>>:cio_linux_random.c>
)

target_compile_definitions(PLATFORM PRIVATE _GNU_SOURCE)

set_property(TARGET PLATFORM 
    PROPERTY PUBLIC_HEADER "linux/cio_error_code_impl.h;linux/cio_eventloop_impl.h;linux/cio_server_socket_impl.h;linux/cio_socket_impl.h"
)

target_include_directories(PLATFORM
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../generated/ ${CMAKE_CURRENT_LIST_DIR}/../
)

target_link_libraries(PLATFORM $<$<BOOL:${HAVE_BSD_LIB}>:bsd>)

get_property(targets DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${targets})
	if(CIO_ENABLE_LTO)
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endforeach()
