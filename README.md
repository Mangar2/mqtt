# mqtt-broker

A fully specification-compliant MQTT 5.0 broker written in C++20.

## Run

```sh
# Run with defaults
./build/release/mqtt-broker

# Show CLI help
./build/release/mqtt-broker --help

# Run with config file
./build/release/mqtt-broker path/to/broker.ini

# Run with config file and CLI trace overrides
./build/release/mqtt-broker path/to/broker.ini \
    --trace-level=info \
    --trace-module=broker \
    --trace-module=connection
```

Startup precedence is deterministic:

1. Built-in defaults
2. INI config file
3. CLI trace overrides (`--trace-level`, `--trace-module`)

Any unknown CLI flag causes startup failure.

## Prerequisites

| Tool    | Minimum version | Notes                                  |
|---------|-----------------|----------------------------------------|
| CMake   | 3.16            | Native build without presets           |
| Ninja   | 1.11            | `winget install Ninja-build.Ninja`     |
| Clang   | 16              | `clang++` must be on `PATH`            |

Preset-based workflows (`cmake --preset ...`) require CMake 3.25+.
On older systems (for example Raspberry Pi OS Buster with CMake 3.16), use
explicit `-S/-B` configure commands instead of presets.

### ARM cross-compilation (Raspberry Pi)

Cross-compilation presets exist for:

- Raspberry Pi Zero / Pi 1 (ARMv6): `armv6-debug`, `armv6-release`
- Raspberry Pi Zero 2 (ARMv7 32-bit userspace): `arm-debug`, `arm-release`

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
cmake --preset armv6-release
```

On macOS, native cross-linking for GNU/Linux ARM is usually not available out
of the box. Recommended approach: run the cross-build inside a Linux container
(Docker/Podman) and use the same presets there.

## Build

### Configure

```sh
cmake --preset debug          # Debug build for the host platform
cmake --preset release        # Release build for the host platform
cmake --preset debug-sanitize # Debug + AddressSanitizer + UBSan
cmake --preset armv6-debug    # Debug cross-compile for Raspberry Pi Zero / Pi 1
cmake --preset armv6-release  # Release cross-compile for Raspberry Pi Zero / Pi 1
cmake --preset arm-debug      # Debug cross-compile for Raspberry Pi Zero 2
cmake --preset arm-release    # Release cross-compile for Raspberry Pi Zero 2
```

### Compile

```sh
cmake --build --preset debug
cmake --build --preset release
cmake --build --preset armv6-release
cmake --build --preset arm-release
```

Build artefacts are placed in `build/<preset-name>/`.

## CLI options

The broker supports a positional config path plus tracing flags:

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `<config-path>` | positional path | none | Optional path to an INI config file. Must be the first argument when used. |
| `--help` | flag | off | Prints CLI usage and exits successfully without starting the broker. |
| `--trace-level=<none\|error\|warning\|info\|trace>` | flag | from config/default | Overrides tracing global level. |
| `--trace-module=<module>` | repeatable flag | from config/default | Overrides tracing module list. Repeat flag for multiple modules. |

Notes:

- If at least one `--trace-module` is present, the module list is rebuilt from CLI values.
- If no config path is provided, the broker starts from built-in defaults.
- Any unrecognized flag prints an error and exits with failure.

## INI configuration

### Format rules

- INI style sections: `[section]`
- Key-value pairs: `key = value`
- `#` starts a comment line
- Whitespace around keys/values is trimmed
- Unknown sections and unknown keys are ignored

### Complete option reference

| Section | Key | Type | Default | Range / Values | Description |
|---------|-----|------|---------|----------------|-------------|
| `network` | `mqtt_port` | uint16 | `1883` | `0..65535` | MQTT TCP listener. `0` disables listener. |
| `network` | `ws_port` | uint16 | `0` | `0..65535` | MQTT over WebSocket listener. `0` disables listener. |
| `broker` | `allow_anonymous` | bool | `true` | `true/false`, `1/0`, `yes/no` | Allow anonymous CONNECT without credentials. |
| `broker` | `max_connections` | uint32 | `1000` | `1..100000` | Maximum simultaneous connections. |
| `broker` | `receive_maximum` | uint16 | `65535` | `1..65535` | Server receive maximum for inflight QoS 1/2. |
| `broker` | `server_keep_alive` | uint16 | `0` | `0..65535` | CONNACK Server Keep Alive override. `0` means disabled (use client CONNECT keep alive). |
| `broker` | `session_expiry_max` | uint32 | `0` | `0..4294967295` | Hard cap for Session Expiry Interval. `0` means no hard cap. |
| `broker` | `topic_alias_maximum` | uint16 | `10` | `0..65535` | Maximum topic alias value accepted by broker. |
| `broker` | `max_queued_messages` | uint32 | `100` | `1..100000` | Per-client offline queue capacity. |
| `broker` | `write_queue_max_bytes` | uint32 | `65536` | `1..4194304` | Per-connection outbound write-queue byte capacity (hard-capped). |
| `broker` | `stream_buffer_max_bytes` | uint32 | `1048576` | `1..67108864` | Per-connection inbound stream-buffer hard cap in bytes. |
| `broker` | `qos_retransmit_timeout_seconds` | uint32 | `20` | `>=1` | Timeout before QoS retransmit becomes eligible. |
| `broker` | `tick_interval_ms` | uint32 | `100` | `>=1` | Main broker tick interval in milliseconds. |
| `auth` | `credential` | string | none | `username:password` | Repeatable credential entry for password auth mode. |
| `acl` | `rule` | csv string | none | `effect,principal,action,topic` | Repeatable ACL rule entry, e.g. `deny,anonymous,publish,private/#`. |
| `persistence` | `mode` | enum | `full` | `full/off/no-states` | Persistence mode (`full`: all snapshots, `off`: no persistence, `no-states`: skip in-flight QoS state persistence). |
| `persistence` | `enabled` | bool | legacy | `true/false`, `1/0`, `yes/no` | Backward-compatible alias for mode (`true`→`full`, `false`→`off`). |
| `persistence` | `dir` | path string | `./data` | any path | Snapshot directory path. |
| `tracing` | `global_level` | enum | `warning` | `none/error/warning/info/trace` | Global structured tracing threshold. |
| `tracing` | `trace_modules` | csv string | empty | comma-separated module names | Module-level trace override list. |
| `tracing` | `max_theme_events_per_window` | uint32 | `5` | `1..1048576` | Maximum emitted records per trace `info` theme per 1-second measurement window. |

Validation rules:

- At least one listener must be active: not both `mqtt_port` and `ws_port` equal to `0`.
- Numeric values outside their valid ranges fail startup.

### Example config

```ini
# broker.ini

[network]
mqtt_port = 1883
ws_port = 9001

[broker]
allow_anonymous = false
max_connections = 5000
receive_maximum = 1000
server_keep_alive = 30
session_expiry_max = 86400
topic_alias_maximum = 20
max_queued_messages = 1000
write_queue_max_bytes = 262144
stream_buffer_max_bytes = 1048576
qos_retransmit_timeout_seconds = 20
tick_interval_ms = 100

[auth]
credential = app:secret
credential = admin:another-secret

[acl]
rule = deny,anonymous,publish,private/#
rule = allow,admin,publish_and_subscribe,#

[persistence]
mode = full
dir = ./data

[tracing]
global_level = warning
trace_modules = broker,connection
max_theme_events_per_window = 5
```

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
