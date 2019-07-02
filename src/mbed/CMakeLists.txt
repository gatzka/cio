cmake_minimum_required(VERSION 3.8)

set(CIO_MBED_SOURCE_DIR mbed)

add_custom_target(mbed
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/mbed_package/
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_BINARY_DIR}/mbed_package/
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/*.h ${CMAKE_BINARY_DIR}/mbed_package/

	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/mbed_package/http-parser/
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/http-parser/http_parser.h ${CMAKE_BINARY_DIR}/mbed_package/http-parser/
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/http-parser/http_parser.c ${CMAKE_BINARY_DIR}/mbed_package/http-parser/

	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/sha1/ ${CMAKE_BINARY_DIR}/mbed_package/sha1/

	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/generated/*.h ${CMAKE_BINARY_DIR}/mbed_package/

	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/mbed_package/${CIO_MBED_SOURCE_DIR}/
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/${CIO_MBED_SOURCE_DIR}/*.cpp ${CMAKE_BINARY_DIR}/mbed_package/${CIO_MBED_SOURCE_DIR}/
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/${CIO_MBED_SOURCE_DIR}/*.h ${CMAKE_BINARY_DIR}/mbed_package/${CIO_MBED_SOURCE_DIR}/
)

add_dependencies(mbed generate_semver)

set(CIO_PLATFORM_SOURCES
	#    zephyr/cio_zephyr_eventloop.c
	#    mbed/cio_mbed_random.c
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

