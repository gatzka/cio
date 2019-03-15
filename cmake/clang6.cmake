set(CMAKE_SYSTEM_NAME Linux)

include("${CMAKE_CURRENT_LIST_DIR}/compiler/clang.cmake")

set(CMAKE_C_COMPILER clang-6.0)
set(CMAKE_CXX_COMPILER clang++-6.0)
set(CTEST_COVERAGE_COMMAND "llvm-cov-6.0")
set(CTEST_COVERAGE_EXTRA_FLAGS "gcov")
set(CIO_COVERAGE_FLAGS "--coverage")

set(CIO_ASAN_FLAGS "-g -fsanitize=address -fno-sanitize-recover=all -fno-omit-frame-pointer")
set(CIO_LSAN_FLAGS "-g -fsanitize=leak -fno-sanitize-recover=all -fno-omit-frame-pointer")
set(CIO_UBSAN_FLAGS "-g -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer")
set(CIO_MSAN_FLAGS "-g -fsanitize=memory -fsanitize-memory-track-origins -fno-sanitize-recover=all -fno-omit-frame-pointer")
