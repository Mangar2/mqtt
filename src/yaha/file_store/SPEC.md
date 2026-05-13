# file_store — YAHA FileStore Component

## Purpose

Implements a standalone key/value HTTP store with MQTT monitoring publishes.

## Public API

### Struct `FileStoreMonitoringConfig`

| Field | Type | Default | Notes |
|------|------|---------|-------|
| `enabled` | `bool` | `true` | Enable MQTT monitoring publishes |
| `topicPrefix` | `std::string` | `$MONITOR/FileStore` | Monitoring topic prefix |
| `qos` | `Qos` | `Qos::AtLeastOnce` | QoS for monitoring publishes |
| `retain` | `bool` | `false` | Retain flag for monitoring publishes |
| `watchIntervalMs` | `std::uint32_t` | `1000` | Filesystem polling interval |

### Struct `FileStoreConfig`

| Field | Type | Default | Notes |
|------|------|---------|-------|
| `serverHost` | `std::string` | `127.0.0.1` | HTTP bind host |
| `serverPort` | `std::uint16_t` | `8210` | HTTP listen port (`0` disables HTTP listener) |
| `directory` | `std::filesystem::path` | `data` | Directory containing store files |
| `keepFiles` | `std::uint32_t` | `2` | Number of historic backup files per key to keep |
| `maxKeyLength` | `std::uint32_t` | `100` | Maximum accepted key length |
| `monitoring` | `FileStoreMonitoringConfig` | default | Monitoring behavior |
| `httpStartCallback` | `std::function<void()>` | empty | Optional test hook |
| `httpStopCallback` | `std::function<void()>` | empty | Optional test hook |

### Class `FileStore` : `IMqttComponent`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `explicit FileStore(FileStoreConfig)` | Stores runtime configuration |
| dtor | `~FileStore() override` | Calls `close()` |
| static mapper | `encodeKeyPathToFilename(const std::string&) -> std::string` | Deterministic key-to-filename mapping |
| subscriptions | `getSubscriptions() const -> SubscriptionMap` | Returns empty map |
| inbound | `handleMessage(const Message&)` | No-op |
| publish callback | `setPublishCallback(PublishCallback)` | Stores callback for monitoring emits |
| lifecycle start | `run()` | Starts HTTP listener and watcher loop |
| lifecycle stop | `close()` | Stops watcher and HTTP listener |
| state query | `isRunning() const -> bool` | True while active |

## Behavior

- HTTP `OPTIONS` returns CORS preflight headers.
- HTTP `POST` writes one key payload to filesystem.
- HTTP `GET` reads one key payload and returns JSON value payload.
- Payload format:
  - `content-type: application/json` stores JSON payload as JSON.
  - other content types store payload as text.
- Key length > `maxKeyLength` returns `400`.
- Missing key on `GET` returns `404`.
- Internal filesystem/runtime errors return `500`.
- All HTTP error responses are built from `yaha::YahaError` (`src/yaha/error_handling/yaha_error.h`).
- Error payload contains machine code, technical message, user-facing message, and optional debug details.
- FileStore HTTP error codes:
  - `YAHA_FILE_STORE_KEY_TOO_LONG` (400)
  - `YAHA_FILE_STORE_INVALID_JSON_PAYLOAD` (400)
  - `YAHA_FILE_STORE_KEY_NOT_FOUND` (404)
  - `YAHA_FILE_STORE_PERSIST_FAILED` (500)
  - `YAHA_FILE_STORE_READ_FAILED` (500)
- Monitoring publishes to `<topicPrefix>/created|changed|deleted|error`.
- Monitoring trigger sources:
  - successful HTTP `POST` write (`source=http-post`),
  - filesystem watcher create/change/delete (`source=filesystem-watch`).
- Monitoring publish path is delivery-result aware via component publish callback.
- Failed monitoring sends are queued in bounded retry queue and retried on later activity cycles.
- Retry exhaustion emits explicit structured failure log and drops event.
- Message logging:
  - logs every inbound MQTT message to `std::cout` before handling (`file_store[in] ...`)
  - logs outbound monitoring success only after callback confirms send (`file_store[out] ...`)
  - logs outbound monitoring failure as `file_store[out-fail]` with event type, topic, category, reason, and payload
- File I/O logging:
  - logs read/write lifecycle to `std::cout` (`file_store[file-io] ...`)
  - includes operation (`read|write`), key path, encoded filename, status (`start|ok|error`), and optional error detail

## Files

| File | Role |
|------|------|
| `file_store.h` | API declarations |
| `file_store.cpp` | Implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/file_store_test.cpp` | Unit tests |

## Implementation notes

- Default configuration values in the public config structs are expressed via named constants in `file_store.h` to avoid magic-number literals in member initializers.
- Internal HTTP handlers use named status-code constants in `file_store.cpp` instead of numeric literals.
