Build the mqtt-broker project using CMake presets.

## Available presets

| Preset           | Target                        | Build type |
|------------------|-------------------------------|------------|
| `debug`          | Host platform                 | Debug      |
| `release`        | Host platform                 | Release    |
| `debug-sanitize` | Host platform                 | Debug + ASan/UBSan |
| `arm-debug`      | Raspberry Pi Zero 2 (ARM 32)  | Debug      |
| `arm-release`    | Raspberry Pi Zero 2 (ARM 32)  | Release    |

## Commands

Configure then build:
```sh
cmake --preset <preset>
cmake --build --preset <preset>
```

## Prerequisites

- CMake >= 3.25
- Ninja
- clang++ on PATH

For ARM presets (Linux host only):
```sh
sudo apt-get install binutils-arm-linux-gnueabihf gcc-arm-linux-gnueabihf
```

## Build output

`build/<preset-name>/mqtt-broker` (or `mqtt-broker.exe` on Windows)
