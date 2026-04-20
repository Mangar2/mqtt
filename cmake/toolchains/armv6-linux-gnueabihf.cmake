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

if(NOT DEFINED ENV{ARM_SYSROOT} OR "$ENV{ARM_SYSROOT}" STREQUAL "")
    message(FATAL_ERROR "ARM_SYSROOT is required for armv6 presets. Point it to a Raspberry Pi Zero/1 sysroot to avoid ARMv7 runtime linkage.")
endif()

set(CMAKE_SYSROOT "$ENV{ARM_SYSROOT}")

file(GLOB SYSROOT_GXX_INCLUDE_DIRS LIST_DIRECTORIES true "${CMAKE_SYSROOT}/usr/include/c++/*")
list(SORT SYSROOT_GXX_INCLUDE_DIRS)
list(REVERSE SYSROOT_GXX_INCLUDE_DIRS)
list(GET SYSROOT_GXX_INCLUDE_DIRS 0 SYSROOT_GXX_INCLUDE_DIR)
get_filename_component(SYSROOT_GXX_VERSION "${SYSROOT_GXX_INCLUDE_DIR}" NAME)

if(NOT EXISTS "${SYSROOT_GXX_INCLUDE_DIR}/cstdint")
    message(FATAL_ERROR "Could not find libstdc++ headers in ARM_SYSROOT (${CMAKE_SYSROOT}). Expected cstdint under /usr/include/c++/<version>.")
endif()

set(SYSROOT_GXX_MULTIARCH_INCLUDE_DIR "${CMAKE_SYSROOT}/usr/include/arm-linux-gnueabihf/c++/${SYSROOT_GXX_VERSION}")

# Raspberry Pi Zero/1: ARM1176JZF-S (ARMv6), VFP, hard-float ABI
set(ARMV6_COMMON_FLAGS "--sysroot=${CMAKE_SYSROOT} -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard -marm")
set(CMAKE_C_FLAGS_INIT   "${ARMV6_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ARMV6_COMMON_FLAGS} -stdlib=libstdc++ -isystem ${SYSROOT_GXX_INCLUDE_DIR} -isystem ${SYSROOT_GXX_MULTIARCH_INCLUDE_DIR} -isystem ${SYSROOT_GXX_INCLUDE_DIR}/backward")
set(CMAKE_EXE_LINKER_FLAGS_INIT "--sysroot=${CMAKE_SYSROOT} -L${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf -L${CMAKE_SYSROOT}/lib/arm-linux-gnueabihf -Wl,-rpath-link,${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf -Wl,-rpath-link,${CMAKE_SYSROOT}/lib/arm-linux-gnueabihf")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT}")

# Never search host paths for target libraries/headers
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
