# file_store_client — YAHA FileStore Runtime Types and Domain Mapping

## Purpose

Defines runtime config structures and INI mapping helpers for the standalone FileStore executable.

## Public API

### Struct `FileStoreClientRuntimeConfig`

| Field | Type | Notes |
|------|------|-------|
| `storeConfig` | `FileStoreConfig` | FileStore HTTP/persistence/monitoring domain settings |
| `mqttConfig` | `YahaMqttClient::Config` | Generic MQTT runtime settings |

### Functions

| Function | Signature | Notes |
|---|---|---|
| `loadFileStoreConfigFromIni` | `(const IniDocument&) -> FileStoreConfigLoadResult` | Parses domain-specific FileStore config |
| `loadFileStoreClientRuntimeConfigFromIni` | `(const IniDocument&) -> FileStoreClientRuntimeConfigLoadResult` | Parses full runtime config including MQTT settings |

## Configuration format

Supported INI sections:

- `[mqtt]`
  - `host`, `port`, `clientId`, `reconnectDelayMs`, `keepAliveIntervalMs`, `loopSleepMs`
- `[server]`
  - `host`, `port`
- `[filestore]`
  - `directory`, `keepFiles`, `maxKeyLength`
- `[monitoring]`
  - `enabled`, `topicPrefix`, `qos`, `retain`, `watchIntervalMs`

## Files

| File | Role |
|------|------|
| `file_store_client_app.h` | Runtime config declarations |
| `file_store_client_app.cpp` | INI mapping implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/file_store_client_app_test.cpp` | Unit tests |
