cmake_minimum_required(VERSION 3.8)


set(CIO_PLATFORM_SOURCES
)

target_sources(${PROJECT_NAME}
	PRIVATE ${CIO_PLATFORM_SOURCES}
)

set_property(TARGET ${PROJECT_NAME} APPEND
    PROPERTY PUBLIC_HEADER "zephyr/cio_error_code_impl.h"
	#PROPERTY PUBLIC_HEADER "zephyr/cio_error_code_impl.h;linux/cio_eventloop_impl.h;linux/cio_linux_socket.h;linux/cio_linux_socket_utils.h;linux/cio_server_socket_impl.h;linux/cio_socket_impl.h"
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
)

