cmake_minimum_required(VERSION 3.0)
project(cio-autobahn C)

add_executable(autobahn_server
    autobahn_server.c
)
target_link_libraries (autobahn_server cio:cio)

