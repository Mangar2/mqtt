# yahabroker

A fully specification-compliant MQTT 5.0 broker written in C++20.

## Run

```sh
# Run with defaults
./build/release/yahabroker

# Show CLI help
./build/release/yahabroker --help

# Run with config file
./build/release/yahabroker path/to/broker.ini

# Run with mosquitto-style config argument
./build/release/yahabroker -c path/to/broker.ini

# Run with config file and CLI trace overrides
./build/release/yahabroker path/to/broker.ini \
    --port=1884 \
    --trace-level=info \
    --trace-module=broker \
    --trace-module=connection

# Run YAHA MessageStore client with default config path (broker.ini)
./build/release/yahamsgstoreclient

# Run YAHA BrokerConnector client with default config path (broker.ini)
./build/release/yahabrokerconnectorclient

# Run YAHA BrokerConnector client with explicit config path
./build/release/yahabrokerconnectorclient path/to/broker.ini

# Run YAHA HTTP MQTT interface client with default config path (broker.ini)
./build/release/yahahttpmqttinterfaceclient

# Run YAHA HTTP MQTT interface client with explicit config path
./build/release/yahahttpmqttinterfaceclient path/to/broker.ini

# Run YAHA RemoteService client with default config path (broker.ini)
./build/release/yaharemoteserviceclient

# Run YAHA RemoteService client with explicit config path
./build/release/yaharemoteserviceclient path/to/broker.ini

# Run YAHA MessageStore client with explicit config path
./build/release/yahamsgstoreclient path/to/broker.ini

# Enable MQTT sent/received message tracing (lifecycle tracing is always on)
./build/release/yahamsgstoreclient path/to/broker.ini --trace-messages

# Print YAHA MessageStore client version
./build/release/yahamsgstoreclient --version

# Run the Step 27 test-client shell
./build/release/yahatestclient --help

# Save a reusable connection profile
./build/release/yahatestclient save-profile \
    --output ./test-client.profile \
    --host 127.0.0.1 \
    --port 1883 \
    --transport mqtt \
    --client-id shell-client

# Connect using profile + overrides and keep session open until Ctrl+C
./build/release/yahatestclient connect \
    --profile ./test-client.profile \
    --keep-alive-seconds 30

# Publish one message with MQTT 5 property options
./build/release/yahatestclient publish \
    --profile ./test-client.profile \
    --topic demo/step29 \
    --qos 1 \
    --payload "hello" \
    --payload-encoding raw \
    --content-type text/plain

# List built-in Step 31 scenarios
./build/release/yahatestclient scenario --list-scenarios

# Run one built-in Step 31 scenario
./build/release/yahatestclient scenario \
    --scenario qos1_subscribe_publish_unsubscribe \
    --profile ./test-client.profile

# Run Step 32 publish-rate load mode with JSON metrics output
./build/release/yahatestclient scenario \
    --load-mode publish-rate \
    --connection-count 20 \
    --message-interval-ms 10 \
    --publish-limit 500 \
    --topic-template bench/topic/{index} \
    --client-template bench-client-{index} \
    --metrics-json \
    --profile ./test-client.profile
```

Startup precedence is deterministic:

1. Built-in defaults
2. INI config file
3. CLI overrides (`--port`, `--trace-level`, `--trace-module`)

Any unknown CLI flag causes startup failure.

## Deployment logging

YAHA deployment service units are configured with dedicated systemd journal namespaces.
Each managed unit (`broker`, `filestore`, `msgstore`, `autom`, `valuesvc`,
`brkconn`, `httpmqtt`, `remotesvc`) writes to its own namespace to prevent
cross-service log eviction.

Deployment installer behavior:

- installs namespace journald drop-ins under `/etc/systemd/journald@<namespace>.conf.d/`
- enables persistent storage and namespace-local retention limits
- keeps existing namespace journal data on repeated deployment runs

The `svc` helper reads logs from the namespace assigned to the selected unit:

```sh
./deployment/yaha/svc log valuesvc 200
```

## Deployment packaging and rollout

Create deployment artifacts directory and zip archive (`deployment/yaha` and `deployment/yaha.zip`):

```sh
python3 cmake/create_yaha_deployment.py --preset armv7-zig-release --build
```

Create artifacts and copy deployment zip plus local apply script to a remote target via `scp`:

```sh
python3 cmake/create_yaha_deployment.py --preset armv7-zig-release --build \
    --remote pi@yaha2:~/mqtt
```

Run on the remote host to unpack and deploy locally with checksum-based copy rules:

```sh
bash deploy.sh --zip yaha.zip --target-dir ~/mqtt
```

`deploy.sh` behavior:

- unpacks deployment zip locally on the remote host
- copies files with checksum compare and skips identical files
- protects existing changed `.ini` files (prompt by default)
- supports `--yes-overwrite-ini` and `--no-overwrite-ini`
- restarts only changed services (component install runs only when files changed)

`yahamsgstoreclient` accepts one optional positional config path and optional flags:

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `<config-path>` | positional path | `broker.ini` | Optional path to MessageStore INI config. |
| `--trace-messages` | flag | off | Enables printing MQTT sent/received message lines to stdout. |
| `--version`, `-V` | flag | off | Prints executable version and exits successfully. |
| `-h`, `--help` | flag | off | Prints CLI usage and exits successfully. |

`yahabrokerconnectorclient` accepts one optional positional config path:

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `<config-path>` | positional path | `broker.ini` | Optional path to BrokerConnector INI config. |
| `-h`, `--help` | flag | off | Prints CLI usage and exits successfully. |

`yahaautomationclient` accepts one optional positional config path and one optional flag:

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `<config-path>` | positional path | `broker.ini` | Optional path to Automation client INI config. |
| `--trace-messages` | flag | off | Enables MQTT transport sent/received trace lines in runtime client. |
| `-h`, `--help` | flag | off | Prints CLI usage and exits successfully. |

`yahahttpmqttinterfaceclient` accepts one optional positional config path:

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `<config-path>` | positional path | `broker.ini` | Optional path to HTTP MQTT interface client INI config. |
| `-h`, `--help` | flag | off | Prints CLI usage and exits successfully. |

`yaharemoteserviceclient` accepts one optional positional config path and one optional flag:

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `<config-path>` | positional path | `broker.ini` | Optional path to RemoteService INI config. |
| `--trace-messages` | flag | off | Enables MQTT transport sent/received trace lines in runtime client. |
| `-h`, `--help` | flag | off | Prints CLI usage and exits successfully. |

Automation client INI sections:

- `[mqtt]`: generic MQTT runtime settings (host, port, clientId, reconnectDelayMs, keepAliveIntervalMs, loopSleepMs)
- `[filestore]`: `use`, `host`, `port`, `path`
- `[monitoring]`: `topicPrefix`
- `[automation]`: `topicPrefix`, `managementTopicPrefix`, `longitude`, `latitude`, `subscribeQoS`, `logIncomingMessages`, `logOutgoingMessages`

BrokerConnector INI sections:

- `[sourceHttpBroker]`: `host`, `port`, `clientId`, `clean`, `keepAliveSeconds`, `listenerHost`, `listenerBindHost`, `listenerPort`, `logReason` (`true/false`, default `true`)
- repeated `[subscription]`: `topic` and `qos` (`0`, `1`, `2`), default `topic=#` with `qos=1` when missing
- `[receiverMqttBroker]`: `host`, `port`, `clientId`, `reconnectDelayMs`, `keepAliveSeconds`, `loopSleepMs`, `enableLifecycleTrace`, `enableMessageTrace`, `logReason`
- `[automation]`: `reconnectDelayMs`, `sourceLoopSleepMs`, `sourceKeepAliveIntervalMs`, `maxPublishRetries`, `publishRetryBackoffMs`, `normalizeQosToAtLeastOnce`, `retainPassthrough`
- `[monitoring]`: `sourceLifecycleTrace`, `logReason` (`true/false`, default `true`)

BrokerConnector runtime logging details:

- inbound source callback logging prints parsed receive fields (`topic`, `qos`, `retain`, `dup`, `value`)
- outbound receiver publish logging prints raw payload at send point when available (`raw="..."`)
- `logReason=true` adds one plain reason string (`reason="..."`) without count/latest summary

MessageStore client INI sections:

- `[mqtt]`: generic MQTT runtime settings (host, port, clientId, reconnectDelayMs, keepAliveIntervalMs, loopSleepMs, enableLifecycleTrace)
- `[server]`: `host`, `port`, `path`
- `[persist]`: `directory`, `filename`, `intervalMs`, `keepFiles`
- repeated `[subscription]`: `topic` and `qos` (`0`, `1`, `2`), default `topic=#` with `qos=1` when missing
- `[messagestore]`: `cleanupTopic`, `logIncomingMessages`, `logReason` (`true/false`, `1/0`, `yes/no`, `on/off`, default `true`)
- `[tree]`: `maxHistoryLength`, `historyHysterese`, `maxValuesPerHistoryEntry`, `lengthForFurtherCompression`, `upperBoundFactor`, `upperBoundAddInMilliseconds`, `lowerBoundFactor`, `lowerBoundSubInMilliseconds`

HTTP MQTT interface client INI sections:

- `[httpMqttInterface]`: `listenerHost`, `listenerPort`, `enablePublishPhpAlias`, `useLegacyPhpResponse`

RemoteService client INI sections:

- `[mqtt]`: generic MQTT runtime settings (host, port, clientId, reconnectDelayMs, keepAliveIntervalMs, loopSleepMs)
- `[filestore]`: `host`, `port`, `filename`, `topicPrefix`
- `[remoteservice]`: `listenHost`, `listenPort`, `subscribeQoS`

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

Cross-compilation presets exist for Raspberry Pi Zero / Pi 1 (ARMv6) using Zig:

- `armv6-zig-debug`
- `armv6-zig-release`

This path does not require an external ARM sysroot.
Install Zig on the build host:

```sh
brew install zig
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
cmake --preset armv6-zig-debug    # Debug cross-compile for Raspberry Pi Zero / Pi 1
cmake --preset armv6-zig-release  # Release cross-compile for Raspberry Pi Zero / Pi 1
```

### Compile

```sh
cmake --build --preset debug
cmake --build --preset release
cmake --build --preset armv6-zig-release
```

Build artefacts are placed in `build/<preset-name>/`.

## CLI options

The broker supports a positional config path and mosquitto-style options:

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `<config-path>` | positional path | none | Optional path to an INI config file. Must be the first argument when used. |
| `-c <path>`, `--config-file <path>`, `--config-file=<path>` | flag | none | Loads config from file (alternative to positional `<config-path>`). |
| `-h`, `--help` | flag | off | Prints CLI usage and exits successfully without starting the broker. |
| `-d`, `--daemon` | flag | off | Runs broker in background on POSIX systems. |
| `-p <0..65535>`, `--port <0..65535>`, `--port=<0..65535>` | flag | from config/default | Overrides MQTT listener port (`network.mqtt_port`). Repeatable, last value wins. |
| `-q`, `--quiet` | flag | off | Disables structured tracing output (`trace level = none`). Overrides `--verbose`. |
| `-v`, `--verbose` | flag | off | Enables most detailed structured tracing (`trace level = trace`). |
| `--test-config` | flag | off | Validates config file and exits without starting broker. Requires `<config-path>` or `-c/--config-file`. |
| `--trace-level=<none\|error\|warning\|info\|trace>` | flag | from config/default | Overrides tracing global level. |
| `--trace-level <none\|error\|warning\|info\|trace>` | flag | from config/default | Same as `--trace-level=...`. |
| `--trace-module=<module>`, `--trace-module <module>` | repeatable flag | from config/default | Overrides tracing module list. Repeat flag for multiple modules. |

Notes:

- If at least one `--trace-module` is present, the module list is rebuilt from CLI values.
- If no config path is provided, the broker starts from built-in defaults.
- `<config-path>` and `-c/--config-file` must not be used together.
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
│       └ armv6-zig.cmake             # ARMv6 Zig cross-compilation toolchain
├ src/
│   └ main.cpp
├ spec/
│   ├ implementierungsplan.md         # Module implementation plan
│   └ anforderungskatalog.md          # Full MQTT 5.0 requirements catalogue
├ CMakeLists.txt
└ CMakePresets.json
```

## Limitations

Security notice:

- This broker is not security-hardened and does not meet production security standards.
- Use it only in a trusted and isolated home/lab environment.

| Feature | Status | Notes |
|---------|--------|-------|
| **TLS / MQTTS** (port 8883) | Not implemented | Module 14.1 requires an external TLS library (OpenSSL, mbedTLS, etc.). Use a reverse proxy (nginx, HAProxy, stunnel) to terminate TLS in front of the broker. |
| **WSS** (WebSocket over TLS) | Not implemented | Depends on TLS — same note as above. |
| **MQTT protocol downgrade** | Not supported | MQTT 5 only. MQTT 3.1.1/3.1 downgrade or compatibility mode is intentionally not provided. |
| WebSocket / WS (plain) | Implemented | Module 14.2 — no external dependencies. |

## Feature highlights

- MQTT 5 protocol support is implemented to the best of current project knowledge.
- More than 1000 unit tests.
- More than 300 Python integration tests.

## Client Library Configuration (Step 25)

The public client API (`src/client_api`) provides a unified `ClientConfig`
object for all tunable client-side settings.

Key fields:

- Broker target: `broker_host`, `broker_port`, `transport` (`Tcp`/`WebSocket`)
- Identity and auth: `client_id`, optional username/password credentials
- CONNECT behaviour: `clean_start`, `keep_alive_seconds`,
    `session_expiry_interval_seconds`, `receive_maximum`, `topic_alias_maximum`
- Reconnect policy: `reconnect_backoff`
- Timeouts: per-operation defaults (`connect/publish/subscribe/unsubscribe/
    disconnect`)

`SyncClient` and `AsyncClient` can be constructed directly from `ClientConfig`.
No-timeout operation overloads use the configured timeout defaults.

## Test Client Shell (Steps 27-32)

The repository now builds a standalone test-client executable:

- Binary: `yahatestclient`
- Scope: MQTT 5.0 only
- Transports: `mqtt` and `ws` only
- TLS: intentionally unsupported (`mqtts`/`wss` not available)

Supported subcommands:

- `connect` — connect and keep the session open until signal (`Ctrl+C`)
- `publish` — connect, publish one message, wait for QoS completion, exit
- `subscribe` — connect, subscribe with MQTT 5 options, print/save incoming messages
- `sub` — mqttx compatibility alias for `subscribe` (implemented runtime path)
- `simulate` — mqttx compatibility alias for scenario execution surface (`-sc`/`-f` selectors)
- `ls` — mqttx-style scenario listing alias (`ls --scenarios` / `ls -sc`)
- `init` — initialize default options profile (`--output` optional)
- `check` — print runtime capability summary
- `conn` — mqttx compatibility command stub (help/discoverability surface)
- `scenario` — list built-in scenarios, run Step 31 scripted scenarios, or run Step 32 load modes with metrics output
- `save-profile` — save reusable profile file
- `show-profile` — print effective profile after load+override merge

Top-level options:

- `--help`
- `--version`, `-v`

WP2 shared connection semantics for mqttx-compatible paths:

- Reconnect settings are applied for one-shot `publish` and Step 32 direct load operations: `-rp`/`--reconnect-period`, `--maximum-reconnect-times`.
- Compatibility alias spelling `--maximun-reconnect-times` is accepted and mapped to `maximum_reconnect_times`.
- Recognized but currently out-of-scope options in mqttx-compatible paths are rejected explicitly: `--debug`, `--save-options`, `--load-options`.

WP3 bench runtime semantics (`bench pub`):

- Persistent benchmark connections are used (no reconnect-per-message loop).
- `--count` controls persistent connection pool size.
- `--interval` controls spacing while establishing benchmark connections.
- `--message-interval` controls spacing between publish operations.
- `--limit 0` means unlimited publish loop.
- `--split [delimiter]` splits configured payload into publish sequence tokens.
- `-S`, `--payload-size` generates fixed-size payload per publish operation.
- `-v`, `--verbose` enables bench operation traces and does not imply metrics-json output.

WP4 publish feature semantics (`pub` and `bench pub`):

- `bench pub` applies publish property flags in runtime packets: `-d`, `-pf`, `-e`, `-ta`, `-rt`, `-cd`, `-si`, `-ct`, and `-up`.
- `bench pub` applies publish payload encoding semantics from `-f/--format`.
- `pub -S/--payload-size` generates randomized payload data with the configured payload size.
- `pub` validates schema options when selected by payload encoding:
    - `-Pp/--protobuf-path` + `-Pmn/--protobuf-message-name` for `-f protobuf`
    - `-Ap/--avsc-path` for `-f avro`

WP5 subscribe feature semantics (`sub` and `bench sub`):

- `sub` is implemented as mqttx-compatible alias path for `subscribe`.
- `sub`/`bench sub` apply subscribe option semantics in runtime packets: `-q`, `-nl`, `-rap`, `-rh`, `-si`.
- `sub` supports output pipeline aliases: `--output-mode`, `--file-write`, `--file-save`, `--delimiter`, and `-f/--format`.
- `sub` parses and validates schema input options for payload format selection: `-Pp`, `-Pmn`, `-Ap`.

WP6 simulate and maintenance command semantics:

- `simulate` parses mqttx-style simulation selectors and control aliases (`-sc`, `-f`, `-c`, `-i`, `-im`, `-L`, `-t`, `-I`) and maps Step32 mode names (`mass-connect`, `publish-rate`, `multi-subscribe`) to load-mode execution.
- `ls --scenarios` and `ls -sc` map to scenario catalog listing output.
- `init` writes default profile content (or explicit `--output` path).
- `check` prints runtime capability summary with explicit MQTT5/non-TLS scope.

Profile precedence is deterministic:

1. Built-in defaults
2. Loaded profile (`--profile`)
3. CLI overrides

Supported profile keys:

- `host`, `port`, `transport`
- `ws_path`, repeatable `ws_header`
- `client_id`, `clean_start`, `keep_alive_seconds`
- `username`, `password`
- `session_expiry_interval_seconds`, `receive_maximum`, `maximum_packet_size`, `topic_alias_maximum`
- `request_response_information`, `request_problem_information`, repeatable `connect_user_property`
- `authentication_method`, `authentication_data`
- will options: `will_topic`, `will_payload`, `will_qos`, `will_retain`, `will_delay_interval_seconds`, `will_payload_format_indicator`, `will_message_expiry_interval_seconds`, `will_content_type`, `will_response_topic`, `will_correlation_data`, repeatable `will_user_property`
- publish options: `publish_topic`, `publish_qos`, `publish_retain`, `publish_dup`, `publish_payload`, `publish_payload_stdin`, `publish_payload_stdin_multiline`, `publish_payload_file`
- publish schema and generated payload options: `publish_protobuf_path`, `publish_protobuf_message_name`, `publish_avsc_path`, `publish_payload_size`
- publish encoding options: `publish_payload_encoding` (`raw|json|hex|base64|binary|protobuf|avro`), `publish_correlation_data_encoding` (`raw|hex|base64`)
- publish MQTT 5 property options: `publish_payload_format_indicator`, `publish_message_expiry_interval_seconds`, `publish_topic_alias`, `publish_response_topic`, `publish_correlation_data`, `publish_subscription_identifier`, `publish_content_type`, repeatable `publish_user_property`
- subscribe options: repeatable `subscribe_entry` (`filter|qos|no_local|retain_as_published|retain_handling`), `subscribe_identifier`, repeatable `subscribe_user_property`, `subscribe_payload_format`, `subscribe_protobuf_path`, `subscribe_protobuf_message_name`, `subscribe_avsc_path`
- subscriber output options: `subscribe_clean_output`, `subscribe_verbose_packets`, `subscribe_output_file`, `subscribe_output_file_save`, `subscribe_output_append`, `subscribe_output_delimiter`, `subscribe_output_format`, `subscribe_message_limit`, `subscribe_wait_timeout_ms`
- scenario options: `--scenario <name>`, `--list-scenarios`, `--load-mode <mass-connect|publish-rate|multi-subscribe>`
- Step 32 load controls: `--connection-count`, `--connect-interval-ms`, `--message-interval-ms`, `--publish-limit`, `--topic-template`, `--client-template`, `--metrics-json`
- `reconnect_period_ms`, `maximum_reconnect_times`

## Integration test runner

Run integration tests with:

```bash
python3 test/run_integration_tests.py [options]
```

Useful selection options:

- `--filter <selector>`: run tests matching exact name or path prefix (repeatable).
- `--from_test <selector>`: start at the first test matching the selector.
- `--to_test <selector>`: end at the last test matching the selector.

`--from_test` and `--to_test` define an inclusive range in the internal test order.
They cannot be combined with `--filter` or `--only-failed`.

## Implementation notes

- The codebase is generated almost entirely with AI support.
- Project guidance and guardrails are documented in the skill files inside the repository.

## License

This project is licensed under the Apache License 2.0.
See [LICENSE](LICENSE) for the full text.
