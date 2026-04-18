# mqtt-broker

A fully specification-compliant MQTT 5.0 broker written in C++20.

## Prerequisites

| Tool    | Minimum version | Notes                                  |
|---------|-----------------|----------------------------------------|
| CMake   | 3.25            |                                        |
| Ninja   | 1.11            | `winget install Ninja-build.Ninja`     |
| Clang   | 16              | `clang++` must be on `PATH`            |

### ARM cross-compilation (Raspberry Pi Zero 2)

Cross-compilation is performed on a **Linux host** using Clang's built-in
`--target` support. No separate clang binary is needed, but the ARM sysroot
and linker stubs must be present:

```sh
sudo apt-get install \
    binutils-arm-linux-gnueabihf \
    gcc-arm-linux-gnueabihf \
    libstdc++-12-dev-armhf-cross
```

To use a custom sysroot, set the `ARM_SYSROOT` environment variable before
configuring:

```sh
export ARM_SYSROOT=/path/to/your/sysroot
cmake --preset arm-release
```

## Build

### Configure

```sh
cmake --preset debug          # Debug build for the host platform
cmake --preset release        # Release build for the host platform
cmake --preset debug-sanitize # Debug + AddressSanitizer + UBSan
cmake --preset arm-debug      # Debug cross-compile for Raspberry Pi Zero 2
cmake --preset arm-release    # Release cross-compile for Raspberry Pi Zero 2
```

### Compile

```sh
cmake --build --preset debug
cmake --build --preset release
cmake --build --preset arm-release
```

Build artefacts are placed in `build/<preset-name>/`.

## Project layout

```
mqtt/
├ cmake/
│   └ toolchains/
│       └ arm-linux-gnueabihf.cmake   # ARM cross-compilation toolchain
├ src/
│   └ main.cpp
├ spec/
│   ├ implementierungsplan.md         # Module implementation plan
│   └ anforderungskatalog.md          # Full MQTT 5.0 requirements catalogue
├ CMakeLists.txt
└ CMakePresets.json
```

## Limitations

| Feature | Status | Notes |
|---------|--------|-------|
| **TLS / MQTTS** (port 8883) | Not implemented | Module 14.1 requires an external TLS library (OpenSSL, mbedTLS, etc.). Use a reverse proxy (nginx, HAProxy, stunnel) to terminate TLS in front of the broker. |
| **WSS** (WebSocket over TLS) | Not implemented | Depends on TLS — same note as above. |
| WebSocket / WS (plain) | Implemented | Module 14.2 — no external dependencies. |

## License

TBD
