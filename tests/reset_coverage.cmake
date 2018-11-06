file(GLOB_RECURSE gcda_files "${BINARY_DIR}/*.gcda")
if (gcda_files)
    file(REMOVE ${gcda_files})
endif()

execute_process(
    COMMAND ${LCOV_PATH} --directory ${BINARY_DIR} --zerocounters --quiet --rc lcov_branch_coverage=1
    WORKING_DIRECTORY ${BINARY_DIR}
)
