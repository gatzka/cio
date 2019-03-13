set(CMAKE_SYSTEM_NAME Linux)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_C_FLAGS_INIT "-pipe -fno-common")
set(CTEST_COVERAGE_COMMAND "gcov")
set(CIO_COVERAGE_FLAGS "--coverage")

set(CIO_ASAN_FLAGS "-g -fsanitize=address -fno-sanitize-recover=all -fno-omit-frame-pointer")

# Don't use leak sanitizer and undefined behavior sanitizer in ctest CI. gcc does not write a logfile on which ctest relies on.
set(CIO_LSAN_FLAGS "-g -fsanitize=leak -fno-sanitize-recover=all -fno-omit-frame-pointer")
set(CIO_UBSAN_FLAGS "-g -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer")
