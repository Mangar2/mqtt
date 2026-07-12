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
  - `directory`, `keepFiles`, `maxKeyLength`, `logIncomingMessages`, `logOutgoingMessages`
- `[monitoring]`
  - `enabled`, `topicPrefix`, `qos`, `retain`, `watchIntervalMs`

Validation and fallback behavior:
- Invalid recoverable values do not abort config loading.
- For invalid recoverable values, loader writes deterministic warning logs to
  `std::cerr` and keeps the current default value.
- `monitoring.watchIntervalMs` uses `1..uint32_max` (removed arbitrary upper cap).
- Runtime config load does not fail when MQTT sub-loader rejects one value;
  FileStore keeps MQTT defaults and logs warning context.
- `filestore.logIncomingMessages`/`logOutgoingMessages` are parsed through the shared
  message-log INI helper (`tryLoadMessageLogConfigFromIni`), same as every other YAHA
  client's own message-flow logging flags. Defaults (`false`/`true` respectively) preserve
  prior behavior: incoming logging was previously unavailable (off), outgoing monitoring
  logging was previously always on.

## Files

| File | Role |
|------|------|
| `file_store_client_app.h` | Runtime config declarations |
| `file_store_client_app.cpp` | INI mapping implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/file_store_client_app_test.cpp` | Unit tests |
