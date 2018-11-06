execute_process(
    COMMAND ${LCOV_PATH} --directory ${BINARY_DIR} --capture --quiet --rc lcov_branch_coverage=1 --output-file coverage.info 
    WORKING_DIRECTORY ${BINARY_DIR}
)

execute_process(
    COMMAND ${LCOV_PATH} --remove coverage.info */tests/* */http-parser/* */miniz/* */sha1/* */unity/* /usr/include/* --output-file coverage.cleaned.info --rc lcov_branch_coverage=1
    WORKING_DIRECTORY ${BINARY_DIR}
)

execute_process(
    COMMAND ${GENHTML_PATH} -o ${BINARY_DIR}/coverage coverage.cleaned.info --branch-coverage --quiet
    WORKING_DIRECTORY ${BINARY_DIR}
)
