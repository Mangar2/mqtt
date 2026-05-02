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
- Monitoring publishes to `<topicPrefix>/created|changed|deleted|error`.
- Monitoring trigger sources:
  - successful HTTP `POST` write (`source=http-post`),
  - filesystem watcher create/change/delete (`source=filesystem-watch`).

## Files

| File | Role |
|------|------|
| `file_store.h` | API declarations |
| `file_store.cpp` | Implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/file_store_test.cpp` | Unit tests |
