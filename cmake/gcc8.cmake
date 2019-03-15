set(CMAKE_SYSTEM_NAME Linux)

include("${CMAKE_CURRENT_LIST_DIR}/compiler/gcc8.cmake")

set(CMAKE_C_COMPILER gcc-8)
set(CMAKE_CXX_COMPILER g++-8)
set(CTEST_COVERAGE_COMMAND "gcov-8")

set(CIO_ASAN_FLAGS "-g -fsanitize=address -fno-sanitize-recover=all -fno-omit-frame-pointer")

# Don't use leak sanitizer and undefined behavior sanitizer in ctest CI. gcc does not write a logfile on which ctest relies on.
set(CIO_LSAN_FLAGS "-g -fsanitize=leak -fno-sanitize-recover=all -fno-omit-frame-pointer")
set(CIO_UBSAN_FLAGS "-g -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer")
