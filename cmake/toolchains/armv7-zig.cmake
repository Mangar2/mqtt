# Cross-compilation toolchain for ARMv7 32-bit via Zig
# Target: Raspberry Pi Zero 2 (Cortex-A53, hard-float, musl)
#
# No sysroot required. Zig bundles musl libc for all targets.
#
# Prerequisites:
#   brew install zig
#
# Usage:
#   cmake --preset armv7-zig-release
#   cmake --build --preset armv7-zig-release

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

find_program(ZIG_EXECUTABLE zig REQUIRED)

set(CMAKE_C_COMPILER   ${ZIG_EXECUTABLE})
set(CMAKE_C_COMPILER_ARG1 "cc")
set(CMAKE_CXX_COMPILER ${ZIG_EXECUTABLE})
set(CMAKE_CXX_COMPILER_ARG1 "c++")

# ARMv7 hard-float, musl libc — statically linked, no sysroot needed
set(ZIG_TARGET_FLAGS "-target arm-linux-musleabihf -mcpu=cortex_a53")

set(CMAKE_C_FLAGS_INIT   "${ZIG_TARGET_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ZIG_TARGET_FLAGS}")

# Never search host paths for target libraries/headers
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
