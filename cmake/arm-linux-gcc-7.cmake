set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

include("${CMAKE_CURRENT_LIST_DIR}/compiler/gcc8.cmake")

set(CMAKE_C_COMPILER arm-linux-gnueabi-gcc-7)
set(CMAKE_CXX_COMPILER arm-linux-gnueabi-g++-7)
set(CMAKE_CROSSCOMPILING_EMULATOR "qemu-arm" "-L" "/usr/arm-linux-gnueabi/")
