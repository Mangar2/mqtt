Build the mqtt-broker project using CMake presets.

## Available presets

| Preset           | Target                        | Build type |
|------------------|-------------------------------|------------|
| `debug`          | Host platform                 | Debug      |
| `release`        | Host platform                 | Release    |
| `debug-sanitize` | Host platform                 | Debug + ASan/UBSan |
| `test-coverage`  | Host platform                 | Debug + coverage instrumentation |
| `arm-debug`      | Raspberry Pi Zero 2 (ARM 32)  | Debug      |
| `arm-release`    | Raspberry Pi Zero 2 (ARM 32)  | Release    |

All commands are run from the **project root**: `c:\Development\mqtt`

## First build (or after deleting the build directory)

```sh
cmake --preset debug
cmake --build --preset debug
```

## Incremental build

CMake detects added/removed files automatically via `CONFIGURE_DEPENDS`:

```sh
cmake --build --preset debug
```

## Build + run all tests

```sh
cmake --build --preset debug && ctest --preset debug
```

## Full clean rebuild + tests

```sh
cmake --build --preset debug --clean-first && ctest --preset debug
```

## Build output

| File | Description |
|------|-------------|
| `build/debug/mqtt-broker.exe` | Broker binary |
| `build/debug/mqtt-broker-tests.exe` | Test binary |

## Running tests directly (with filter)

```sh
# All tests
./build/debug/mqtt-broker-tests.exe

# Tests for one module (by tag)
./build/debug/mqtt-broker-tests.exe [message]
./build/debug/mqtt-broker-tests.exe [packet]

# Single test by name
./build/debug/mqtt-broker-tests.exe "message_defaults"

# List all registered tests
./build/debug/mqtt-broker-tests.exe --list-tests
```

## With memory and undefined-behaviour checks

```sh
cmake --preset debug-sanitize
cmake --build --preset debug-sanitize
ctest --preset debug-sanitize
```

## Coverage measurement

`ctest --preset test-coverage` is NOT used here — ctest spawns one process per test,
each overwriting `default.profraw`. Run the binary directly instead:

```sh
cmake --preset test-coverage
cmake --build --preset test-coverage
LLVM_PROFILE_FILE="build/test-coverage/mqtt-tests.profraw" \
    build/test-coverage/mqtt-broker-tests.exe
llvm-profdata merge -sparse build/test-coverage/mqtt-tests.profraw \
    -o build/test-coverage/mqtt-tests.profdata
llvm-cov report build/test-coverage/mqtt-broker-tests.exe \
    -instr-profile=build/test-coverage/mqtt-tests.profdata \
    --ignore-filename-regex="(catch2|_deps|test)"
```

Note: `constexpr`-only functions show 0% by design — they are evaluated at
compile time and are not reachable by runtime instrumentation. Use `STATIC_CHECK`
in tests to verify them.

## Prerequisites

- CMake >= 3.25
- Ninja
- clang++ on PATH

For ARM presets (Linux host only):
```sh
sudo apt-get install binutils-arm-linux-gnueabihf gcc-arm-linux-gnueabihf
```
