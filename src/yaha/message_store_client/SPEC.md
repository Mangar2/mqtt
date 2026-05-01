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
- `[subscriptions]`
  - each key is topic filter, each value is QoS (`0`, `1`, `2`)

When `[subscriptions]` is missing or empty, default subscription is `#` with QoS 1.

## Runtime composition behavior

`src/yaha_msgstoreclient_main.cpp` composes runtime directly:

- start order: `MessageStore::run()` then `YahaMqttClient::run()`
- stop order: `YahaMqttClient::close()` then `MessageStore::close()`

## Runtime console output

The standalone executable prints startup/shutdown status lines to stdout.
Broker session lifecycle output is emitted by the generic `YahaMqttClient` layer so
all YAHA apps can share the same non-domain runtime behavior.

- startup banner with config path and effective MQTT/HTTP/persistence/subscription settings
- HTTP endpoint status (`listening` or `disabled` when `server.port=0`)
- MQTT lifecycle logs from generic client (`connect`, `connected`, `reconnect`, `reconnected`, `subscribe`, `unsubscribe`, `disconnect`)
- optional MQTT message logs (`sent`, `recv`) when enabled by CLI flag `--trace-messages`
- signal handling and shutdown progress lines (`received`, `disconnecting`, `shutting down`, `stopped`)

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
