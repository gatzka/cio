set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ppc)

set(CMAKE_C_COMPILER powerpc-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER powerpc-linux-gnu-g++)
set(CMAKE_C_FLAGS_INIT "-pipe -fno-common")
set(CMAKE_CROSSCOMPILING_EMULATOR "qemu-ppc" "-L" "/usr/powerpc-linux-gnu/")
