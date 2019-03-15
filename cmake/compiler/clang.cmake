include("${CMAKE_CURRENT_LIST_DIR}/clang_gcc.cmake")
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -Wdocumentation")
SET(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld")
