# remote_service — YAHA RemoteService Domain Config, Mapping Lifecycle, and Command Resolution

## Purpose

Defines RemoteService domain-level configuration, FileStore mapping payload
parser helpers, IMqttComponent lifecycle behavior for startup load and
monitor-triggered reload, and phase-3 command resolution/publish handoff API.

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

### Struct `RemoteServiceCommandRequest`

| Field | Type | Notes |
|------|------|-------|
| `path` | `std::string` | Exact service path key |
| `deviceId` | `std::string` | Device key inside selected service mapping |
| `state` | `Value` | Requested command state payload |
| `token` | `std::string` | Token field from HTTP contract carried into domain API |

### Enum `RemoteServiceCommandStatus`

| Value | Meaning |
|------|---------|
| `Success` | Resolution succeeded and publish handoff succeeded |
| `ServiceNotFound` | Unknown service path or unknown device id |
| `PublishFailed` | Publish callback missing or callback threw |

### Struct `RemoteServiceCommandResult`

| Field | Type | Notes |
|------|------|-------|
| `status` | `RemoteServiceCommandStatus` | Domain result code |
| `resolvedMessage` | `std::optional<Message>` | Outbound MQTT message when resolution succeeded |

Member function:

- `isSuccess() -> bool`: returns `true` for `status == Success`.

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
| `resolveCommand` | `RemoteServiceCommandResult(const RemoteServiceCommandRequest&) const` | Resolves one request into outbound MQTT message |
| `publishCommand` | `RemoteServiceCommandResult(const RemoteServiceCommandRequest&)` | Resolves and publishes through callback with domain error mapping |

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

Lifecycle logging:

- startup load logs one success/failure line with trigger `startup`
- monitor reload logs one matched/ignored trigger line
- monitor reload success/failure logs include trigger `monitor`
- duplicate-path mapping warnings continue to log to `std::cerr`

### Command resolution and publish handoff

- `resolveCommand()` performs deterministic mapping:
	- exact service lookup by `request.path`
	- exact device lookup by `request.deviceId`
	- builds outbound message with:
		- mapped topic from service devices map
		- payload from `request.state`
		- qos from service entry (or default from mapping model)
		- reason entry from service reason text
		- retain set to `false`
- Unknown service path or device id returns `ServiceNotFound`.
- `publishCommand()` calls `resolveCommand()` and publishes resolved message via injected callback.
- Callback missing or callback exception returns `PublishFailed`.
- Callback success returns `Success`.

Phase-4 integration note:

- `remote_service_http` module delegates HTTP request inputs to
	`publishCommand()` and maps domain status to HTTP response contract.

## Files

| File | Role |
|------|------|
| `remote_service_config.h` | Domain config type and default constants |
| `remote_service_component.h` | Mapping parser helper declarations and component API |
| `remote_service_component.cpp` | Mapping parser and component lifecycle implementation |