cmake_minimum_required(VERSION 3.8)

set(CIO_PLATFORM_SOURCES
    zephyr/cio_zephyr_eventloop.c
    zephyr/cio_zephyr_random.c
)

target_sources(${PROJECT_NAME}
	PRIVATE ${CIO_PLATFORM_SOURCES}
)

target_include_directories(${PROJECT_NAME}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
)

target_link_libraries (cio PRIVATE zephyr)

