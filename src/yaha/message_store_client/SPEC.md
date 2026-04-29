# message_store_client — YAHA MessageStore Standalone Composition

## Purpose

Composes the MessageStore component with the reusable YahaMqttClient session driver for
standalone process execution. This module owns runtime configuration loading, lifecycle
coordination, and process-level startup/shutdown orchestration.

## Public API

### Struct `MessageStoreClientRuntimeConfig`

| Field | Type | Notes |
|------|------|-------|
| `storeConfig` | `MessageStoreConfig` | MessageStore settings (subscriptions, HTTP, persistence, tree) |
| `mqttConfig` | `YahaMqttClient::Config` | MQTT session runtime settings |

### Class `MessageStoreClientApp`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `MessageStoreClientApp(MessageStoreClientRuntimeConfig)` | creates MessageStore and YahaMqttClient |
| `run` | `void()` | starts MessageStore first, then MQTT session loop |
| `close` | `void()` | stops MQTT session first, then MessageStore |
| `isRunning` | `bool() const` | true when both components are running |
| `tryLoadConfigFromFile` | `static bool(const filesystem::path&, MessageStoreClientRuntimeConfig&, string&)` | parses INI-like config and validates numeric ranges |

## Configuration format

INI-like key-value file with optional sections:

- `[mqtt]`
  - `host`, `port`, `clientId`, `reconnectDelayMs`, `keepAliveIntervalMs`, `loopSleepMs`
- `[server]`
  - `port`, `path`
- `[persist]`
  - `directory`, `filename`, `intervalMs`, `keepFiles`
- `[messagestore]`
  - `cleanupTopic`
- `[subscriptions]`
  - each key is topic filter, each value is QoS (`0`, `1`, `2`)

When `[subscriptions]` is missing or empty, default subscription is `#` with QoS 1.

## Lifecycle behavior

- `run()` starts MessageStore HTTP/persistence lifecycle and then starts MQTT client loop.
- `close()` stops MQTT client before stopping MessageStore to avoid new inbound dispatch
  during shutdown.

## Transport note

The current composition wires an in-memory transport callback bundle so process wiring and
component lifecycle can run without binding to broker-transport internals. Replacing this
bundle with a real broker transport adapter is a runtime integration step and does not
change MessageStore or YahaMqttClient APIs.

## Files

| File | Role |
|------|------|
| `message_store_client_app.h` | Composition and config-loading API declarations |
| `message_store_client_app.cpp` | Runtime parser and lifecycle implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/message_store_client_app_test.cpp` | Unit tests |
