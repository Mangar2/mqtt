# Cross-compilation toolchain for ARMv6 32-bit
# Target: Raspberry Pi Zero / Raspberry Pi 1 (armv6l, hard-float userspace)
# Compiler: Clang with explicit --target triple (no separate clang++ binary needed)
#
# Prerequisites on Debian/Ubuntu host:
#   sudo apt-get install binutils-arm-linux-gnueabihf \
#                        gcc-arm-linux-gnueabihf \
#                        libstdc++-12-dev-armhf-cross
#
# Optional: override sysroot via environment variable:
#   export ARM_SYSROOT=/path/to/your/sysroot
#   cmake --preset armv6-release

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CROSS_TRIPLE arm-linux-gnueabihf)

# Clang cross-compiles via --target; no separate clang++ binary needed
set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_C_COMPILER_TARGET   ${CROSS_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${CROSS_TRIPLE})

# Raspberry Pi Zero/1: ARM1176JZF-S (ARMv6), VFP, hard-float ABI
# Pin Clang to GCC 12 cross runtime to match older Pi userspace ABI.
set(GCC12_INSTALL_DIR "/usr/lib/gcc-cross/arm-linux-gnueabihf/12")
set(CMAKE_C_FLAGS_INIT   "--gcc-install-dir=${GCC12_INSTALL_DIR} -march=armv6zk -mfpu=vfp -mfloat-abi=hard -marm")
set(CMAKE_CXX_FLAGS_INIT "--gcc-install-dir=${GCC12_INSTALL_DIR} -nostdlib++ -march=armv6zk -mfpu=vfp -mfloat-abi=hard -marm")

# Prefer target sysroot library directories at link time.
# This keeps both glibc and libstdc++ aligned with the Raspberry Pi runtime.
if(DEFINED ENV{ARM_SYSROOT})
    string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT
        " -L$ENV{ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf"
        " -L$ENV{ARM_SYSROOT}/lib/arm-linux-gnueabihf"
        " -Wl,-rpath-link,$ENV{ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf"
        " -Wl,-rpath-link,$ENV{ARM_SYSROOT}/lib/arm-linux-gnueabihf")
endif()

# Link standard C++ runtime explicitly from sysroot (paired with -nostdlib++).
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "-lstdc++ -lm -lc")

# Sysroot is optional. On Debian/Ubuntu cross packages, forcing
# /usr/arm-linux-gnueabihf can break linker path resolution.
# Only use a sysroot when explicitly provided by the caller.
if(DEFINED ENV{ARM_SYSROOT})
    set(CMAKE_SYSROOT "$ENV{ARM_SYSROOT}")
endif()

# Never search host paths for target libraries/headers
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
