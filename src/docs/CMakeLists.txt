cmake_minimum_required(VERSION 3.0)
project("cio documentation" C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../../cmake/)
include(generateVersion)

GenerateVersion(../version "cio")

configure_file(../cio_version.h.in ${PROJECT_BINARY_DIR}/generated/cio_version.h)

# check if Doxygen is installed
find_package(Doxygen)
if (DOXYGEN_FOUND)
    # set input and output files
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # request to configure the file
    set(CIO_DOXY_INPUT ". ${PROJECT_BINARY_DIR}/generated/")
    set(CIO_DOXY_STRIP_FROM_PATH "${PROJECT_BINARY_DIR}/generated/ ${CMAKE_CURRENT_SOURCE_DIR}/../")
    set(CIO_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT})
    message("Doxygen build started")

    # note the option ALL which allows to build the docs together with the application
    add_custom_target( doc_doxygen ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM )
else (DOXYGEN_FOUND)
  message("Doxygen need to be installed to generate the doxygen documentation")
endif (DOXYGEN_FOUND)
