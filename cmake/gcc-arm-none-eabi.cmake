set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_C_FLAGS_INIT "-mthumb -mcpu=cortex-m3 --specs=rdimon.specs")
set(CMAKE_CXX_FLAGS_INIT "-mthumb -mcpu=cortex-m3 --specs=rdimon.specs")
set(CMAKE_CROSSCOMPILING_EMULATOR "qemu-arm -cpu cortex-m3")
