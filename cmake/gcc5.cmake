set(CMAKE_SYSTEM_NAME Linux)

include("${CMAKE_CURRENT_LIST_DIR}/compiler/clang_gcc.cmake")

set(CMAKE_C_COMPILER gcc-5)
set(CMAKE_CXX_COMPILER g++-5)
set(CTEST_COVERAGE_COMMAND "gcov-5")

