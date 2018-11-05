cmake_minimum_required(VERSION 3.0)
project(cio_unit_test C)
enable_testing()

add_executable(test_cio_linux_endian test_cio_linux_endian.c)
target_link_libraries(test_cio_linux_endian fff unity cio:cio)
add_test(NAME cio.linux.test_endian COMMAND test_cio_linux_endian)

add_executable(test_cio_linux_epoll test_cio_linux_epoll.c)
target_link_libraries(test_cio_linux_epoll fff unity cio:cio)
add_test(NAME cio.linux.test_epoll COMMAND test_cio_linux_epoll)

add_executable(test_cio_linux_server_socket test_cio_linux_server_socket.c)
target_link_libraries(test_cio_linux_server_socket fff unity cio:cio)
add_test(NAME cio.linux.test_server_socket COMMAND test_cio_linux_server_socket)

add_executable(test_cio_linux_socket test_cio_linux_socket.c)
target_link_libraries(test_cio_linux_socket fff unity cio:cio)
add_test(NAME cio.linux.test_socket COMMAND test_cio_linux_socket)

add_executable(test_cio_linux_socket_utils test_cio_linux_socket_utils.c)
target_link_libraries(test_cio_linux_socket_utils fff unity cio:cio)
add_test(NAME cio.linux.test_socket_utils COMMAND test_cio_linux_socket_utils)

add_executable(test_cio_linux_timer test_cio_linux_timer.c)
target_link_libraries(test_cio_linux_timer fff unity cio:cio)
add_test(NAME cio.linux.test_timer COMMAND test_cio_linux_timer)
