# zwave — YAHA ZWave Domain Config and Service Component

## Purpose

Provides ZWave domain configuration contracts and the phase-3 service component
that orchestrates MQTT routing and controller lifecycle.

## Public API

### struct ZwaveUsbConfig

| Field | Type | Notes |
|------|------|-------|
| `device` | `std::string` | USB device path |
| `topic` | `std::string` | Controller status topic |

### struct ZwaveDeviceConfig

| Field | Type | Notes |
|------|------|-------|
| `topic` | `std::string` | Base MQTT topic |
| `nodeId` | `std::uint16_t` | Node id (`1..255`) |
| `classId` | `std::optional<std::uint16_t>` | Optional command class |
| `instance` | `std::optional<std::uint8_t>` | Optional instance |
| `index` | `std::optional<std::uint8_t>` | Optional index |
| `type` | `std::optional<std::string>` | Optional type hint |
| `label` | `std::optional<std::string>` | Optional label |

### struct ZwaveConfig

| Field | Type | Notes |
|------|------|-------|
| `subscribeQos` | `Qos` | Default `AtLeastOnce` |
| `qos` | `Qos` | Default `AtLeastOnce` |
| `retain` | `bool` | Default `false` |
| `logLevel` | `std::uint8_t` | OpenZWave/service event logging level (`0..4`), default `2` |
| `pollIntervalMs` | `std::uint32_t` | OpenZWave poll interval in milliseconds, default `500` |
| `commandReactionPollIntervalMs` | `std::uint32_t` | Poll interval for tracked command confirmation, default `500` |
| `commandReactionTimeoutMs` | `std::uint32_t` | Timeout for tracked command confirmation, default `30000` |
| `logIncomingMessages` | `bool` | Default `false`; logs inbound MQTT messages handled by ZWave service |
| `logOutgoingMessages` | `bool` | Default `false`; logs outbound MQTT messages emitted by ZWave service |
| `usb` | `ZwaveUsbConfig` | Required |
| `devices` | `std::vector<ZwaveDeviceConfig>` | Required non-empty list |

### class ZwaveServiceComponent

| Function | Signature | Notes |
|---------|-----------|-------|
| constructor | `(ZwaveConfig, std::shared_ptr<IZwaveController>)` | Wires controller callback and initial device config |
| `setDeviceConfiguration` | `(const std::vector<ZwaveDeviceConfig>&)` | Updates controller config and publishes reload info |
| `getSubscriptions` | `() const -> SubscriptionMap` | Returns management + device `/set` subscriptions |
| `handleMessage` | `(const Message&)` | Routes management commands and regular set messages |
| `run` | `() -> void` | Publishes restart markers and requests all config params |
| `close` | `() -> void` | Delegates close to controller |
| `setPublishCallback` | `(PublishCallback)` | Stores outbound publish callback |

## Behavior

## Subscriptions

- Fixed management topics with QoS 2:
	- `system/zwave/removefailednode/set`
	- `system/zwave/addnode/set`
	- `system/zwave/scan/set`
- Device topics from config:
	- with `classId`: `<topic>/set`
	- without `classId`: `<topic>/+/set`
	- qos = `subscribeQos`

## Inbound routing

`handleMessage(...)`:
- optional inbound message-flow log line via shared message logging service (`component="zwave_service" direction="incoming" ...`) when `logIncomingMessages=true`
- important event/error logs `zwave_service[event|error] ...` when `logLevel>=1`
- remove-failed topic -> `controller.removeFailedNode(...)`
	- success status: `system/zwave/removefailednode` value `deleted`
- add-node topic -> `controller.addDevice()`
	- command payload semantics:
		- start inclusion for values `on|true|1|start|now|enable|enabled`
		- disable inclusion mode for values `off|false|0|stop|cancel|disable|disabled`
		- unsupported payloads are rejected, inclusion mode remains `off`, and an operation error publish is emitted
- scan topic -> `controller.startScan()` with deterministic success/failure publish:
	- success: `system/zwave/scan` value `on`
	- failure: `system/zwave/scan` value `off`
	- unknown scan exceptions are contained and reported with reason `unknown`
- other topics:
	- preserves incoming reason list order exactly as received
	- inserts reason `received by zwave service` directly after the incoming reasons
	- never sorts reasons by timestamp
	- routes to `controller.setValue(topic, value, reasons)`
- remove-failed/add-node/setValue exceptions are contained and emitted as deterministic
	`$MONITOR/zwave/error` messages with operation reason metadata.

## Publish flow

- Controller publishes are forwarded without service-side reason merge.
- Outbound messages are emitted with configured publish flags:
	- `qos = config.qos`
	- `retain = config.retain`
- optional outbound message-flow log line via shared message logging service (`component="zwave_service" direction="outgoing" ...`) only after successful callback publish when `logOutgoingMessages=true`
- outgoing message trace logs stay independent from `logLevel`
- Publish callback missing/non-success/exception branches emit deterministic
	shared structured outgoing logs with `event=publish_failed`, reason markers, and optional category/detail metadata.

## Lifecycle

- `setDeviceConfiguration(...)` publishes `$MONITOR/zwave/info` value `configuration reloaded`.
- `run()` publishes startup markers:
	- `system/zwave/removefailednode` value `0`
	- `system/zwave/addnode` value `off`
	- `system/zwave/scan` value `off`
	- reason `zwave restarted`
- `run()` then calls `controller.requestConfigParametersForAllNodes()`.
- request-config exceptions are contained and emitted as deterministic
	`$MONITOR/zwave/error` messages.
- `close()` delegates to controller close.
- `close()` controller exceptions are contained and emitted as deterministic
	`$MONITOR/zwave/error` messages.

## Files

| File | Role |
|------|------|
| `zwave_config.h` | Domain configuration contracts |
| `zwave_service_component.h` | Service component declarations |
| `zwave_service_component.cpp` | Service component implementation |

## Phase-6 test coverage

Unit/component/runtime-integration verification for this module is provided by:

- `test/TEST_SPEC.md`
- `test/zwave_service_component_test.cpp`

Covered phase-6 behavior:

- subscription derivation behavior from `ZwaveConfig`
- inbound MQTT routing for management topics and regular `/set` messages
- controller publish path integrity with reply matcher merge + configured qos/retain
- startup lifecycle markers in `run()` and controller sync request
- shutdown lifecycle delegation in `close()`
