cmake_minimum_required(VERSION 3.9)
project(cio-autobahn C)

add_executable(autobahn_server
    autobahn_server.c
)
target_link_libraries (autobahn_server cio:cio)
set_target_properties(autobahn_server PROPERTIES
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT output)

if(ipo_supported)
    set_property(TARGET autobahn_server PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
