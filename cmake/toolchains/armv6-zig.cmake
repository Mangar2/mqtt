# Cross-compilation toolchain for ARMv6 32-bit via Zig
# Target: Raspberry Pi Zero / Raspberry Pi 1 (arm1176jzf-s, hard-float, musl)
#
# No sysroot required. Zig bundles musl libc for all targets.
#
# Prerequisites:
#   brew install zig
#
# Usage:
#   cmake --preset armv6-zig-release
#   cmake --build --preset armv6-zig-release

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

find_program(ZIG_EXECUTABLE zig REQUIRED)

set(CMAKE_C_COMPILER   ${ZIG_EXECUTABLE})
set(CMAKE_C_COMPILER_ARG1 "cc")
set(CMAKE_CXX_COMPILER ${ZIG_EXECUTABLE})
set(CMAKE_CXX_COMPILER_ARG1 "c++")

# ARMv6 hard-float, musl libc — statically linked, no sysroot needed
set(ZIG_TARGET_FLAGS "-target arm-linux-musleabihf -mcpu=arm1176jzf_s")

set(CMAKE_C_FLAGS_INIT   "${ZIG_TARGET_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ZIG_TARGET_FLAGS}")

# Never search host paths for target libraries/headers
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
