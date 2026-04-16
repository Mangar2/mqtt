# Cross-compilation toolchain for ARM 32-bit
# Target: Raspberry Pi Zero 2 (Cortex-A53 in 32-bit mode, armv7-linux-gnueabihf)
# Compiler: Clang with explicit --target triple (no separate cross-compiler binary needed)
#
# Prerequisites on Debian/Ubuntu host:
#   sudo apt-get install binutils-arm-linux-gnueabihf \
#                        gcc-arm-linux-gnueabihf \
#                        libstdc++-12-dev-armhf-cross
#
# Optional: override sysroot via environment variable:
#   export ARM_SYSROOT=/path/to/your/sysroot
#   cmake --preset arm-release

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CROSS_TRIPLE arm-linux-gnueabihf)

# Clang cross-compiles via --target; no separate clang++ binary needed
set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_C_COMPILER_TARGET   ${CROSS_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${CROSS_TRIPLE})

# Cortex-A53 in 32-bit mode: ARMv7-A, NEON, hard-float ABI
set(CMAKE_C_FLAGS_INIT   "-march=armv7-a -mfpu=neon-fp-armv8 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon-fp-armv8 -mfloat-abi=hard")

# Sysroot: try environment variable first, then the standard Debian cross location
if(DEFINED ENV{ARM_SYSROOT})
    set(CMAKE_SYSROOT "$ENV{ARM_SYSROOT}")
elseif(EXISTS "/usr/arm-linux-gnueabihf")
    set(CMAKE_SYSROOT "/usr/arm-linux-gnueabihf")
endif()

# Never search host paths for target libraries/headers
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
