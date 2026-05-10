# remote_service — YAHA RemoteService Domain Config and Mapping Lifecycle

## Purpose

Defines RemoteService domain-level configuration, FileStore mapping payload
parser helpers, and IMqttComponent lifecycle behavior for startup load and
monitor-triggered reload.

## Public API

### Struct `RemoteServiceConfig`

| Field | Type | Default | Notes |
|------|------|---------|-------|
| `listenHost` | `std::string` | `0.0.0.0` | HTTP listener host |
| `listenPort` | `std::uint16_t` | `9123` | HTTP listener port |
| `subscribeQos` | `Qos` | `Qos::AtLeastOnce` | FileStore monitor subscription QoS |
| `monitorTopicPrefix` | `std::string` | `$MONITOR/FileStore` | FileStore monitor subscription prefix |
| `fileStoreHost` | `std::string` | `127.0.0.1` | FileStore HTTP endpoint host |
| `fileStorePort` | `std::uint16_t` | `8210` | FileStore HTTP endpoint port |
| `mappingKeyPath` | `std::string` | empty | Required FileStore key path for service mapping payload |

### Struct `RemoteServiceServiceMapping`

| Field | Type | Default | Notes |
|------|------|---------|-------|
| `devices` | `std::map<std::string, std::string>` | empty | Device id to outbound topic mapping |
| `qos` | `Qos` | `Qos::AtLeastOnce` | Optional per-service command publish QoS |
| `reason` | `std::string` | `remote command` | Optional per-service command reason |

### Alias `RemoteServiceMap`

`RemoteServiceMap = std::map<std::string, RemoteServiceServiceMapping>`

Exact-path lookup map from HTTP service path to parsed service mapping entry.

### Parser helper functions

| Function | Signature | Notes |
|------|------|------|
| `tryParseRemoteServiceMappingPayload` | `(const std::string&, RemoteServiceMap&, std::string&) -> bool` | Parses and validates FileStore payload object with `services` array |
| `tryExtractFileStoreMonitorKeyPath` | `(const std::string&) -> std::optional<std::string>` | Extracts `keyPath` string from monitor payload |

### Class `RemoteServiceComponent` : `IMqttComponent`

| Member | Signature | Notes |
|------|------|------|
| ctor | `explicit RemoteServiceComponent(RemoteServiceConfig)` | Stores runtime config |
| `getSubscriptions` | `SubscriptionMap() const` | Returns monitor subscription `<monitorTopicPrefix>/#` |
| `handleMessage` | `void(const Message&)` | Triggers reload on matching monitor key path |
| `run` | `void()` | Starts lifecycle and performs startup mapping load |
| `close` | `void()` | Stops lifecycle flag |
| `setPublishCallback` | `void(PublishCallback)` | Stores callback for phase-3 publish handoff |
| `isRunning` | `bool() const` | Lifecycle diagnostic helper |
| `serviceCount` | `std::size_t() const` | Loaded mapping entry count |
| `hasServicePath` | `bool(const std::string&) const` | Exact path existence helper |
| `mappedTopicFor` | `std::optional<std::string>(const std::string&, const std::string&) const` | Device topic lookup helper |

## Behavior

### Mapping payload validation

- Accepts root JSON object with required `services` array.
- Each service entry requires:
	- `path` as non-empty string
	- `devices` as object<string, string>
- Optional fields:
	- `qos` integer in range `0..2`
	- `reason` string
- Validation is all-or-nothing:
	- any invalid structure rejects the full payload
	- output mapping is not modified on failure
- Duplicate-path rule:
	- first occurrence is kept
	- all later duplicates are ignored
	- each ignored duplicate emits one `std::cerr` line

### Startup and reload lifecycle

- `run()` sets lifecycle running and issues one FileStore HTTP `GET <mappingKeyPath>`.
- Successful GET with valid payload atomically replaces full in-memory map.
- Startup load failure keeps empty map and continues runtime.
- `handleMessage()` inspects monitor payloads on `<monitorTopicPrefix>/#`.
- Only monitor events with matching `keyPath == mappingKeyPath` trigger reload.
- Failed reload keeps previous valid map unchanged.

## Files

| File | Role |
|------|------|
| `remote_service_config.h` | Domain config type and default constants |
| `remote_service_component.h` | Mapping parser helper declarations and component API |
| `remote_service_component.cpp` | Mapping parser and component lifecycle implementation |