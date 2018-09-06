FIND_PROGRAM( LCOV_PATH lcov )
FIND_PROGRAM( GENHTML_PATH genhtml )

if(${CMAKE_C_COMPILER_ID} MATCHES "(Apple)?[Cc]lang")
	if("${CMAKE_C_COMPILER_VERSION}" VERSION_LESS 3)
		message(FATAL_ERROR "Clang version must be 3.0.0 or greater! Aborting...")
	endif()
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
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
endif()

if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Coverage"))
  message( WARNING "Code coverage results with an optimized (non-Debug) build may be misleading" )
endif()

function(SETUP_TARGET_FOR_COVERAGE _targetname _outputname _removepatterns)

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
	    if(NOT LCOV_PATH)
	    	message(FATAL_ERROR "lcov not found! Aborting...")
	    endif()

	    if(NOT GENHTML_PATH)
	    	message(FATAL_ERROR "genhtml not found! Aborting...")
	    endif()

	    set(coverage_info "${CMAKE_BINARY_DIR}/${_outputname}.info")
	    set(coverage_cleaned "${coverage_info}.cleaned")
	    separate_arguments(_removepatterns)

	    add_custom_target(${_targetname}
	    	COMMAND ${LCOV_PATH} --directory ${CMAKE_BINARY_DIR} --zerocounters --quiet --rc lcov_branch_coverage=1
	    	COMMAND make test
	    	COMMAND ${LCOV_PATH} --directory ${CMAKE_BINARY_DIR} --capture --quiet --rc lcov_branch_coverage=1 --output-file ${coverage_info}
	    	COMMAND ${LCOV_PATH} --remove ${coverage_info} ${_removepatterns} --output-file ${coverage_cleaned} --rc lcov_branch_coverage=1 --quiet
	    	COMMAND ${GENHTML_PATH} -o ${_outputname} ${coverage_cleaned} --branch-coverage --quiet
	    	WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
	    	COMMENT "Resetting code coverage counters to zero.\nProcessing code coverage counters and generating report."
	    )

	    add_custom_command(TARGET ${_targetname} POST_BUILD
	    	COMMAND ;
	    	COMMENT "Open ${CMAKE_BINARY_DIR}/${_outputname}/index.html in your browser to view the coverage report."
	    )
    endif()
endfunction()

