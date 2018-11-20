if(NOT DEFINED CIO_CTEST_MODEL)
    set(CIO_CTEST_MODEL "Experimental")
endif()

set(CTEST_SOURCE_DIRECTORY "${CTEST_SCRIPT_DIRECTORY}")
set(CTEST_BINARY_DIRECTORY "/tmp/cio/")

cmake_host_system_information(RESULT FQDN QUERY FQDN)

set(CTEST_BUILD_NAME "${FQDN}")
set(CTEST_SITE "${FQDN}")
set(CTEST_CONFIGURATION_TYPE Debug)

#SET (CTEST_ENVIRONMENT "PATH=c:/WINDOWS/system32\;c:/WINDOWS\;c:/Programs/Borland/Bcc55/Bin")

if(NOT CIO_CTEST_MODEL STREQUAL "Experimental")
    ctest_empty_binary_directory(${CTEST_BINARY_DIRECTORY})
endif()

string(TIMESTAMP CIO_CTEST_TIMESTAMP UTC)

include(ProcessorCount)
ProcessorCount(NUMBER_OF_CORES)
if(NUMBER_OF_CORES EQUAL 0)
    set(NUMBER_OF_CORES 1)
endif()

ctest_start(${CIO_CTEST_MODEL})

set(CTEST_CMAKE_GENERATOR "Ninja")
set(COVERAGE_OPTIONS
  "-DCMAKE_TOOLCHAIN_FILE=${CTEST_SCRIPT_DIRECTORY}/cmake/gcc8.cmake"
  "-DCMAKE_C_FLAGS=--coverage"
)

ctest_configure(OPTIONS "${COVERAGE_OPTIONS}")

#set(CTEST_USE_LAUNCHERS 1)

string(LENGTH ${CTEST_BINARY_DIRECTORY} CTEST_BINARY_DIRECTORY_LEN)
MATH(EXPR CTEST_BINARY_DIRECTORY_LEN "${CTEST_BINARY_DIRECTORY_LEN}-1")
string(SUBSTRING ${CTEST_BINARY_DIRECTORY} 0 ${CTEST_BINARY_DIRECTORY_LEN} CIO_OBJECT_DIRECTORY)

find_program(GCOVR_BIN gcovr)
if(GCOVR_BIN)
    set(CTEST_CUSTOM_POST_TEST
        "mkdir -p ${CTEST_BINARY_DIRECTORY}/cov-html/${CIO_CTEST_TIMESTAMP}/"
        "${GCOVR_BIN} --gcov-executable=gcov-8 --html --html-details --html-title cio -f ${CTEST_SCRIPT_DIRECTORY}/src/\\* -e ${CTEST_SCRIPT_DIRECTORY}/src/http-parser/\\* -e ${CTEST_SCRIPT_DIRECTORY}/src/miniz/\\* -e ${CTEST_SCRIPT_DIRECTORY}/src/sha1/\\* --exclude-directories .\\*CompilerIdC\\* -r ${CTEST_SCRIPT_DIRECTORY} --object-directory=${CIO_OBJECT_DIRECTORY} -o ${CTEST_BINARY_DIRECTORY}/cov-html/${CIO_CTEST_TIMESTAMP}/index.html"
    )
endif()

set(CTEST_COVERAGE_COMMAND "gcov-8")
set(CTEST_CUSTOM_COVERAGE_EXCLUDE
${CTEST_CUSTOM_COVERAGE_EXCLUDE}
    ".*/tests/.*"
    ".*/src/http-parser/.*"
    ".*/src/sha1/.*"
)

if(CTEST_CMAKE_GENERATOR STREQUAL "Unix Makefiles")
    set(CTEST_BUILD_FLAGS -j${NUMBER_OF_CORES})
endif()
ctest_build()
ctest_test(PARALLEL_LEVEL ${NUMBER_OF_CORES})
ctest_coverage()
if(GCOVR_BIN)
    message(" -- Open ${CTEST_BINARY_DIRECTORY}cov-html/${CIO_CTEST_TIMESTAMP}/index.html to see collected coverage")
endif()


find_program(CTEST_MEMORYCHECK_COMMAND NAMES valgrind)
set(CTEST_MEMORYCHECK_TYPE Valgrind)
set(CTEST_MEMORYCHECK_COMMAND_OPTIONS "--errors-for-leak-kinds=all --show-leak-kinds=all --leak-check=full --error-exitcode=1")
#set(CTEST_MEMORYCHECK_SUPPRESSIONS_FILE ${CTEST_SOURCE_DIRECTORY}/tests/valgrind.supp)
ctest_memcheck(PARALLEL_LEVEL ${NUMBER_OF_CORES})

# unset(CTEST_MEMORYCHECK_COMMAND)
# unset(CTEST_MEMORYCHECK_COMMAND_OPTIONS)
# set(CTEST_MEMORYCHECK_TYPE UndefinedBehaviorSanitizer)
# ctest_memcheck(PARALLEL_LEVEL ${NUMBER_OF_CORES})


# include(CTestCoverageCollectGCOV)
# ctest_coverage_collect_gcov(
#     TARBALL gcov.tar
#     SOURCE ${CTEST_SOURCE_DIRECTORY}
#     BUILD ${CTEST_BINARY_DIRECTORY}
#     GCOV_COMMAND ${CTEST_COVERAGE_COMMAND}
# )
# if(EXISTS "${CTEST_BINARY_DIRECTORY}/gcov.tar")
#     ctest_submit(CDASH_UPLOAD "${CTEST_BINARY_DIRECTORY}/gcov.tar"
#     CDASH_UPLOAD_TYPE GcovTar)
# endif()
# 
# ctest_submit()
