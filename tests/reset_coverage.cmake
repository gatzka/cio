file(GLOB_RECURSE gcda_files "${BINARY_DIR}/*.gcda")
if (gcda_files)
    file(REMOVE ${gcda_files})
endif()

file(GLOB_RECURSE gcno_files "${BINARY_DIR}/*.gcno")
if (gcno_files)
    file(REMOVE ${gcno_files})
endif()

execute_process(
    COMMAND ${LCOV_PATH} --directory ${BINARY_DIR} --zerocounters --quiet --rc lcov_branch_coverage=1
    WORKING_DIRECTORY ${BINARY_DIR}
)
