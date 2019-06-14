cmake_minimum_required(VERSION 3.8)

if(NOT CIO_MBED_SOURCES)
    message(FATAL_ERROR "No path to mbed sources given!")
endif()

set(CIO_PLATFORM_SOURCES
	#    zephyr/cio_zephyr_eventloop.c
    mbed/cio_mbed_random.c
	#    zephyr/cio_zephyr_server_socket.c
	#    zephyr/cio_zephyr_timer.c
)

target_sources(${PROJECT_NAME}
	PRIVATE ${CIO_PLATFORM_SOURCES}
)

target_include_directories(${PROJECT_NAME}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
	PRIVATE ${CIO_MBED_SOURCES}
	PRIVATE ${CIO_MBED_TARGET_DIR}
)

