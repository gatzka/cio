cmake_minimum_required(VERSION 3.0)
project(cio VERSION 0.0.1 LANGUAGES C)

add_library(${PROJECT_NAME} SHARED
    "cio_base64.c"
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(cio
    PROPERTIES VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}" SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS ${PROJECT_NAME} EXPORT CioTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(EXPORT CioTargets
    FILE CioTargets.cmake
    NAMESPACE cio::
    DESTINATION lib/cmake/cio
)
