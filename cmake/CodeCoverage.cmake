FIND_PROGRAM( LCOV_PATH lcov )
FIND_PROGRAM( GENHTML_PATH genhtml )

if("${CMAKE_C_COMPILER_ID}" MATCHES "(Apple)?[Cc]lang")
	if("${CMAKE_C_COMPILER_VERSION}" VERSION_LESS 3)
		MESSAGE(FATAL_ERROR "Clang version must be 3.0.0 or greater! Aborting...")
	endif()
elseif(NOT ${CMAKE_C_COMPILER_ID} MATCHES "GNU" )
	MESSAGE(FATAL_ERROR "Compiler is not GNU gcc! Aborting...")
endif()

set(CMAKE_C_FLAGS_COVERAGE
    "-g -O0 --coverage -fprofile-arcs -ftest-coverage"
    CACHE STRING "Flags used by the C compiler during coverage builds."
    FORCE )
set(CMAKE_EXE_LINKER_FLAGS_COVERAGE
    ""
    CACHE STRING "Flags used for linking binaries during coverage builds."
    FORCE )
set(CMAKE_SHARED_LINKER_FLAGS_COVERAGE
    ""
    CACHE STRING "Flags used by the shared libraries linker during coverage builds."
    FORCE )
MARK_AS_ADVANCED(
    CMAKE_C_FLAGS_COVERAGE
    CMAKE_EXE_LINKER_FLAGS_COVERAGE
    CMAKE_SHARED_LINKER_FLAGS_COVERAGE )

if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Coverage"))
  MESSAGE( WARNING "Code coverage results with an optimized (non-Debug) build may be misleading" )
endif()

function(SETUP_TARGET_FOR_COVERAGE _targetname _outputname)

	if(NOT LCOV_PATH)
		message(FATAL_ERROR "lcov not found! Aborting...")
	endif()

	if(NOT GENHTML_PATH)
		message(FATAL_ERROR "genhtml not found! Aborting...")
	endif() # NOT GENHTML_PATH

	set(coverage_info "${CMAKE_BINARY_DIR}/${_outputname}.info")
	set(coverage_cleaned "${coverage_info}.cleaned")

	add_custom_target(${_targetname}

		${LCOV_PATH} --directory ${CMAKE_BINARY_DIR} --zerocounters --quiet --rc lcov_branch_coverage=1

		COMMAND make test

		COMMAND ${LCOV_PATH} --directory ${CMAKE_BINARY_DIR} --capture --quiet --rc lcov_branch_coverage=1 --output-file ${coverage_info}
		COMMAND ${LCOV_PATH} --remove ${coverage_info} '*/tests/*' '*/http-parser/*' '*/sha1/*' '*/unity/*' '/usr/include/*' --output-file ${coverage_cleaned} --rc lcov_branch_coverage=1 --quiet
		COMMAND ${GENHTML_PATH} -o ${_outputname} ${coverage_cleaned} --branch-coverage --quiet
		COMMAND ${CMAKE_COMMAND} -E remove ${coverage_info}

		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		COMMENT "Resetting code coverage counters to zero."
	)

	add_custom_command(TARGET ${_targetname} POST_BUILD
		COMMAND ;
		COMMENT "Processing code coverage counters and generating report.\nOpen ${CMAKE_BINARY_DIR}/${_outputname}/index.html in your browser to view the coverage report."
	)

endfunction()

