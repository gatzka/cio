#
# This file is supposed to run in ctest script mode:
# ctest -S <path-to-this-file>/build.cmake
#
# You can set some command line variables to change the
# behaviour of this script:
#
# -DCIO_CTEST_TOOLCHAIN_FILE:STRING=<path-to-toolchain-file>
#
# -DCIO_CTEST_CONFIGURATION_TYPE:STRING=Debug|Release|MemorySanitizer|AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|Valgrind|Coverage
# -DCIO_CTEST_MODEL:STRING=Experimental|Nightly|Continuous
# -DCIO_CTEST_DOCUMENTATION:BOOL=OFF|ON
# -DCIO_CTEST_ANALYZER:STRING=scan-build-<version-number>|clang-tidy-<version-number>
# -DCIO_CTEST_CMAKE_GENERATOR:STRING=Ninja|Unix Makefiles|...)

set(CTEST_USE_LAUNCHERS 1)

if(NOT DEFINED CIO_CTEST_MODEL)
  set(CIO_CTEST_MODEL "Experimental")
endif()

if(DEFINED CIO_CTEST_TOOLCHAIN_FILE)
  set(CONFIGURE_OPTIONS "-DCMAKE_TOOLCHAIN_FILE=${CIO_CTEST_TOOLCHAIN_FILE}")
endif()

if(NOT DEFINED CIO_CTEST_DOCUMENTATION)
    set(CIO_CTEST_DOCUMENTATION OFF)
endif()

set(CTEST_SOURCE_DIRECTORY "${CTEST_SCRIPT_DIRECTORY}")
set(CTEST_BINARY_DIRECTORY "/tmp/cio/")

if(NOT DEFINED CIO_CTEST_CONFIGURATION_TYPE)
    set(CIO_CTEST_CONFIGURATION_TYPE "Debug")
endif()
set(CTEST_CONFIGURATION_TYPE ${CIO_CTEST_CONFIGURATION_TYPE})

if(NOT CIO_CTEST_MODEL STREQUAL "Experimental")
    ctest_empty_binary_directory(${CTEST_BINARY_DIRECTORY})
endif()

if(CTEST_CONFIGURATION_TYPE STREQUAL "AddressSanitizer")
  set(CTEST_MEMORYCHECK_TYPE "AddressSanitizer")
  set(CTEST_MEMORYCHECK_SANITIZER_OPTIONS "verbosity=1:exitcode=-1:check_initialization_order=true:detect_stack_use_after_return=true:strict_init_order=true:detect_invalid_pointer_pairs=10:strict_string_checks=true")
elseif(CTEST_CONFIGURATION_TYPE STREQUAL "MemorySanitizer")
  set(CTEST_MEMORYCHECK_TYPE "MemorySanitizer")
  set(CTEST_MEMORYCHECK_SANITIZER_OPTIONS "verbosity=1:exitcode=-1")
elseif(CTEST_CONFIGURATION_TYPE STREQUAL "LeakSanitizer")
  set(CTEST_MEMORYCHECK_TYPE "LeakSanitizer")
  set(CTEST_MEMORYCHECK_SANITIZER_OPTIONS "verbosity=1:exitcode=-1")
elseif(CTEST_CONFIGURATION_TYPE STREQUAL "UndefinedBehaviorSanitizer")
  set(CTEST_MEMORYCHECK_TYPE "UndefinedBehaviorSanitizer")
  set(CTEST_MEMORYCHECK_SANITIZER_OPTIONS "verbosity=1:exitcode=-1:print_stacktrace=1")
elseif(CTEST_CONFIGURATION_TYPE STREQUAL "Valgrind")
  find_program(CTEST_MEMORYCHECK_COMMAND NAMES valgrind)
  set(CTEST_MEMORYCHECK_TYPE Valgrind)
  set(CTEST_MEMORYCHECK_COMMAND_OPTIONS "--errors-for-leak-kinds=all --show-leak-kinds=all --leak-check=full --error-exitcode=1")
  set(CTEST_CONFIGURATION_TYPE "Debug")
  #set(CTEST_MEMORYCHECK_SUPPRESSIONS_FILE ${CTEST_SOURCE_DIRECTORY}/tests/valgrind.supp)
endif()

if(DEFINED CIO_CTEST_ANALYZER)
  string(REGEX MATCH "^scan-build|^clang-tidy" ANALYZER_TYPE ${CIO_CTEST_ANALYZER})
  if(ANALYZER_TYPE STREQUAL "scan-build")
    set(C_COMPILER_TYPE "clang")
    set(CXX_COMPILER_TYPE "clang++")
    string(REPLACE ${ANALYZER_TYPE} "" COMPILER_VERSION ${CIO_CTEST_ANALYZER})
    set(ENV{CCC_CC} "${C_COMPILER_TYPE}${COMPILER_VERSION}")
    set(ENV{CCC_CXX} "${CXX_COMPILER_TYPE}${COMPILER_VERSION}")
    set(CTEST_CONFIGURE_COMMAND "${CIO_CTEST_ANALYZER} ${CMAKE_COMMAND} -DCMAKE_C_FLAGS=--coverage -fno-inline -fno-inline-small-functions -fno-default-inline ${CTEST_SOURCE_DIRECTORY}")
    set(ANALYZER_REPORT_DIR "${CTEST_BINARY_DIRECTORY}analyzer-scan/")
    set(CTEST_BUILD_COMMAND "${CIO_CTEST_ANALYZER} --status-bugs -o ${ANALYZER_REPORT_DIR} ${CMAKE_COMMAND} --build ${CTEST_BINARY_DIRECTORY}")
    set(IS_CLANG_STATIC_ANALYZER TRUE)
  elseif(ANALYZER_TYPE STREQUAL "clang-tidy")
    set(CONFIGURE_OPTIONS "${CONFIGURE_OPTIONS};-DCMAKE_C_CLANG_TIDY=${CIO_CTEST_ANALYZER}")
  endif()
endif()

###########
cmake_host_system_information(RESULT FQDN QUERY FQDN)

set(CTEST_BUILD_NAME "${FQDN}")
set(CTEST_SITE "${FQDN}")

include(ProcessorCount)
ProcessorCount(NUMBER_OF_CORES)
if(NUMBER_OF_CORES EQUAL 0)
  set(NUMBER_OF_CORES 1)
endif()

ctest_start(${CIO_CTEST_MODEL})

# Configure step

#set(CTEST_CMAKE_GENERATOR "Unix Makefiles")

if(NOT DEFINED CIO_CTEST_CMAKE_GENERATOR)
	set(CIO_CTEST_CMAKE_GENERATOR "Ninja")
endif()
set(CTEST_CMAKE_GENERATOR ${CIO_CTEST_CMAKE_GENERATOR})

set(CONFIGURE_OPTIONS "${CONFIGURE_OPTIONS}")
ctest_configure(OPTIONS "${CONFIGURE_OPTIONS}")
ctest_configure()

# Build step

if(CTEST_CMAKE_GENERATOR STREQUAL "Unix Makefiles")
  #set(CTEST_BUILD_FLAGS "${CTEST_BUILD_FLAGS} VERBOSE=1")
  set(CTEST_BUILD_FLAGS "${CTEST_BUILD_FLAGS} -j${NUMBER_OF_CORES}")
endif()
ctest_build(NUMBER_ERRORS CIO_NUMBER_OF_ERRORS NUMBER_WARNINGS CIO_NUMBER_OF_WARNING)

# Test/Coverage step

find_program(GCOVR_BIN gcovr)
if(CTEST_CONFIGURATION_TYPE STREQUAL "Coverage" AND GCOVR_BIN)
    string(LENGTH ${CTEST_BINARY_DIRECTORY} CTEST_BINARY_DIRECTORY_LEN)
    MATH(EXPR CTEST_BINARY_DIRECTORY_LEN "${CTEST_BINARY_DIRECTORY_LEN}-1")
    string(SUBSTRING ${CTEST_BINARY_DIRECTORY} 0 ${CTEST_BINARY_DIRECTORY_LEN} CIO_OBJECT_DIRECTORY)
    
    string(TIMESTAMP CIO_CTEST_TIMESTAMP UTC)
    set(CIO_CTEST_COVERAGE_DIR "${CTEST_BINARY_DIRECTORY}cov-html/${CIO_CTEST_TIMESTAMP}")
    set(CTEST_CUSTOM_POST_TEST
        "cmake -E make_directory ${CIO_CTEST_COVERAGE_DIR}"
        "${GCOVR_BIN} --html --html-details --html-title cio -f ${CTEST_SCRIPT_DIRECTORY}/src/\\* -e ${CTEST_SCRIPT_DIRECTORY}/src/http-parser/\\* -e ${CTEST_SCRIPT_DIRECTORY}/src/miniz/\\* -e ${CTEST_SCRIPT_DIRECTORY}/src/sha1/\\* --exclude-directories .\\*CompilerIdC\\* -r ${CTEST_SCRIPT_DIRECTORY} --object-directory=${CIO_OBJECT_DIRECTORY} -o ${CIO_CTEST_COVERAGE_DIR}/index.html")
    
    set(CTEST_CUSTOM_COVERAGE_EXCLUDE
    ${CTEST_CUSTOM_COVERAGE_EXCLUDE}
        ".*/tests/.*"
        ".*/src/http-parser/.*"
        ".*/src/sha1/.*"
    )
endif()
 
if(CIO_NUMBER_OF_ERRORS OR CIO_NUMBER_OF_WARNING)
  if (IS_CLANG_STATIC_ANALYZER )
    message(" -- Look into ${ANALYZER_REPORT_DIR} to see clang static analyzer report!")
  endif()
  message(FATAL_ERROR " -- Error or warning occured while building!")
  return()
endif()

if(CIO_CTEST_DOCUMENTATION)
  ctest_build(TARGET docs)
  message(" -- Open ${CTEST_BINARY_DIRECTORY}src/docs/html/index.html to see generated documentation")
endif()

ctest_test(RETURN_VALUE TEST_RETURN PARALLEL_LEVEL 1)
if(TEST_RETURN)
  message(FATAL_ERROR " -- Error while running tests!")
  return()
endif()
 
if(CTEST_CONFIGURATION_TYPE STREQUAL "Coverage" AND GCOVR_BIN)
  ctest_coverage()
  message(" -- Open ${CIO_CTEST_COVERAGE_DIR}/index.html to see collected coverage")
endif()


if(DEFINED CTEST_MEMORYCHECK_TYPE)
  ctest_memcheck(RETURN_VALUE MEMCHECK_RETURN PARALLEL_LEVEL ${NUMBER_OF_CORES})
  if(MEMCHECK_RETURN)
    message(FATAL_ERROR " -- Error while running memcheck!")
    return()
  endif()
endif()

# include(CTestCoverageCollectGCOV)
# ctest_coverage_collect_gcov(
#   TARBALL gcov.tar
#   SOURCE ${CTEST_SOURCE_DIRECTORY}
#   BUILD ${CTEST_BINARY_DIRECTORY}
#   GCOV_COMMAND ${CTEST_COVERAGE_COMMAND}
# )
# if(EXISTS "${CTEST_BINARY_DIRECTORY}/gcov.tar")
#   ctest_submit(CDASH_UPLOAD "${CTEST_BINARY_DIRECTORY}/gcov.tar"
#   CDASH_UPLOAD_TYPE GcovTar)
# endif()
# 
# ctest_submit()
