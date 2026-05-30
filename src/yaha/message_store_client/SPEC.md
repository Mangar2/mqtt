# message_store_client — YAHA MessageStore Runtime Types and Domain Mapping

## Purpose

Defines MessageStore client runtime config data types and MessageStore-specific
config mapping from generic INI documents.

## Public API

### Struct `MessageStoreClientRuntimeConfig`

| Field | Type | Notes |
|------|------|-------|
| `storeConfig` | `MessageStoreConfig` | MessageStore settings (subscriptions, HTTP, persistence, tree) |
| `mqttConfig` | `YahaMqttClient::Config` | MQTT session runtime settings |

No standalone app wrapper class is provided in this module.
Process composition and lifecycle orchestration are handled directly by
`src/yaha_msgstoreclient_main.cpp`.

## Configuration format

INI-like key-value file with optional sections:

Parsing is composed from reusable shared modules:

- `src/yaha/ini/` loads/parses INI and provides typed readers directly on `IniDocument`.
- `src/yaha/mqtt_client/mqtt_client_config.*` maps MQTT and subscription sections.
- `message_store_client_config.*` maps only MessageStore domain fields.

- `[mqtt]`
  - `host`, `port`, `clientId`, `reconnectDelayMs`, `keepAliveIntervalMs`, `loopSleepMs`
- `[server]`
  - `host`, `port`, `path`
- `[persist]`
  - `directory`, `filename`, `intervalMs`, `keepFiles`
- `[messagestore]`
  - `cleanupTopic`
  - `logIncomingMessages`
  - `logReason` (optional, default `true`)
  - `replayLoadedStateFile` (optional JSONL dump path written once after startup restore)
  - `replayIncomingMessagesFile` (optional JSONL append path for all messages received after startup restore)
- `[tree]`
  - `maxHistoryLength`, `historyHysterese`, `maxValuesPerHistoryEntry`
  - `lengthForFurtherCompression` (range `0..uint32_max`; `0` keeps legacy non-converting behavior)
  - `upperBoundFactor`, `upperBoundAddInMilliseconds`
  - `lowerBoundFactor`, `lowerBoundSubInMilliseconds`
- `[subscription]` (repeatable)
  - `topic` topic filter
  - `qos` (`0`, `1`, `2`)

When `[subscription]` is missing or empty, default subscription is `#` with QoS 1.
Invalid `[subscription]` content and legacy `[subscriptions]` format fall back to
default subscription `#` with deterministic warning output.

INI error handling:
- Invalid recoverable values do not abort config loading.
- For invalid recoverable values, loader writes deterministic warning logs to
  `std::cerr` and keeps the current default value.
- `persist.intervalMs` and `persist.keepFiles` use `0..uint32_max`.
- `tree` numeric ranges use technical `uint32`/`double` limits instead of
  arbitrary caps.
- Runtime config load does not fail when MQTT/message-log sub-loaders reject one
  value; defaults are kept with warning context.

## Runtime composition behavior

`src/yaha_msgstoreclient_main.cpp` composes runtime directly:

- start order: `MessageStore::run()` then `YahaMqttClient::run()`
- stop order: `YahaMqttClient::close()` then `MessageStore::close()`

## Runtime console output

The standalone executable prints startup/shutdown status lines to stdout.
Broker session lifecycle output is emitted by the generic `YahaMqttClient` layer so
all YAHA apps can share the same non-domain runtime behavior.

- startup banner with config path and effective MQTT/HTTP/persistence/subscription settings
- startup banner includes executable version and startup activity summary
- HTTP endpoint status (`listening` or `disabled` when `server.port=0`)
- MQTT lifecycle logs from generic client (`connect`, `connected`, `reconnect`, `reconnected`, `subscribe`, `unsubscribe`, `disconnect`)
- optional MQTT message logs (`sent`, `recv`) when enabled by CLI flag `--trace-messages`
- optional incoming-only logs via shared message logging service when INI key `[messagestore] logIncomingMessages=true` and CLI message tracing is off
- incoming logs use deterministic structured fields (`component`, `direction`, `topic`, `value`, `qos`, `retain`, `dup`, `reason`) and include full reason chain when `messagestore.logReason=true`
- `messagestore.logIncomingMessages` and `messagestore.logReason` are mapped through shared message-log INI helper (`tryLoadMessageLogConfigFromIni`) with unchanged key names and defaults
- when configured, replay recorder writes canonical YAHA envelope JSONL startup dump (`replayLoadedStateFile`) and post-restore append stream (`replayIncomingMessagesFile`) for long-run memory reproduction workflows
- signal handling and shutdown progress lines (`received`, `disconnecting`, `shutting down`, `stopped`)
- compression stats header after startup restore (`message_store[stats] phase=start_after_restore`) followed by multiline aligned metric rows (`name : value`)
- compression stats header every 60 seconds while runtime is active (`phase=periodic_60s`) followed by multiline aligned metric rows (`name : value`)
- compression stats header after signal-driven shutdown (`phase=stop_after_signal`) followed by multiline aligned metric rows (`name : value`)

## CLI behavior

`src/yaha_msgstoreclient_main.cpp` supports:

- optional positional `<config-path>` (default `broker.ini`)
- `--trace-messages` for transport-level sent/recv traces
- `--test <input-file>` for synchronous offline ingest benchmark mode (no MQTT/HTTP startup)
- `--test-handshake` to wait for stdin test commands in test mode
- `--test-http-port <0..65535>` to enable HTTP endpoint in synchronous `--test` mode (requires `--test`)
- `--version` (`-V`) to print executable name and semantic version, then exit
- `--help` (`-h`) to print usage

### Test mode input format

In `--test` mode, each non-empty and non-comment line (`#...`) must be one JSON object in canonical YAHA envelope shape:

- `{"message":{"topic":"...","value":...,"reason":[{"message":"...","timestamp":"..."}]}}`

The mode processes lines synchronously via direct MessageStore tree insertion
and prints `messages=<count>` and `buildElapsedMs=<duration>`.

Without `--test-handshake`, it persists one snapshot file and prints `test.save`
with save success flag, save duration, and written file path.

With `--test-handshake`, it prints `test.handshake phase=ready_for_start` before
loading, waits for `load`, then prints `test.handshake phase=ready_for_end` after
loading and accepts test commands:

- `save` persists one snapshot and prints `test.handshake phase=saved index=<n> success=<0|1>`
- `sleep <ms>` sleeps and prints `test.handshake phase=slept ms=<ms>`
- `exit` terminates test mode

When `--test-handshake` is used and no explicit `save` command was received,
one final `test.save` line is still emitted for compatibility.

At test end it also prints `test.stats` with internal compression counters:
current nodes, total stored messages, bucket type counts (`single`, `timeValue`, `time`, `interval`),
and represented logical history message counts per bucket type.

## Transport behavior

Runtime composition in `src/yaha_msgstoreclient_main.cpp` wires the reusable
broker transport factory from `src/yaha/mqtt_client/broker_transport.*` into
`YahaMqttClient::Transport`.
The shared transport adapter itself is implemented on top of core client modules from `src/client`:

- `client/connection_negotiator.h` for TCP dial + CONNECT/CONNACK negotiation.
- MQTT codecs (`codec/packet/*.h`, `codec/packet_reader/packet_reader.h`) for wire encoding/decoding.
- `network/stream_buffer.h` for packet framing over TCP stream reads.

Transport callback behavior:

- `connect`: dials broker endpoint, negotiates CONNECT, initializes stream state.
- `subscribe`: sends SUBSCRIBE and waits for SUBACK.
- `unsubscribe`: sends UNSUBSCRIBE and waits for UNSUBACK.
- `publish`: sends PUBLISH according to YAHA message qos/retain.
- `pollIncoming`: reads inbound packets, maps inbound PUBLISH to YAHA `Message`, and emits PUBACK/PUBREC/PUBCOMP as required.
- `ping`: sends PINGREQ.
- `disconnect`: sends best-effort DISCONNECT and closes socket.

## Files

| File | Role |
|------|------|
| `message_store_client_app.h` | Runtime config value type and loading/mapping API declarations |
| `message_store_client_app.cpp` | Runtime config loading and MessageStore domain mapping implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/message_store_client_app_test.cpp` | Unit tests |
