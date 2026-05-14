# zwave — YAHA ZWave Domain Config and Service Component

## Purpose

Provides ZWave domain configuration contracts and the phase-3 service component
that orchestrates MQTT routing, reply-matcher flow, and controller lifecycle.

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
	- `$MONITOR/zwave/removefailednode/set`
	- `$MONITOR/zwave/addnode/set`
	- `$MONITOR/zwave/scan/set`
- Device topics from config:
	- with `classId`: `<topic>/set`
	- without `classId`: `<topic>/+/set`
	- qos = `subscribeQos`

## Inbound routing

`handleMessage(...)`:
- remove-failed topic -> `controller.removeFailedNode(...)`
- add-node topic -> `controller.addDevice()`
- scan topic -> `controller.startScan()` with deterministic success/failure publish:
	- success: `$MONITOR/zwave/notification` value `scan command accepted`
	- failure: `$MONITOR/zwave/error` value `scan command failed`
	- unknown scan exceptions are contained and reported with reason `unknown`
- other topics:
	- adds reason `received by zwave service`
	- stores incoming message in reply matcher
	- routes to `controller.setValue(topic, value)`
- remove-failed/add-node/setValue exceptions are contained and emitted as deterministic
	`$MONITOR/zwave/error` messages with operation reason metadata.

## Publish and matcher flow

- Controller publishes are passed through reply-matcher update.
- Outbound messages are emitted with configured publish flags:
	- `qos = config.qos`
	- `retain = config.retain`
- Publish callback missing/non-success/exception branches emit deterministic
	`zwave_service[error] op=publish ...` logs.

## Lifecycle

- `setDeviceConfiguration(...)` publishes `$MONITOR/zwave/info` value `configuration reloaded`.
- `run()` publishes startup markers:
	- `$MONITOR/zwave/removefailednode` value `nop`
	- `$MONITOR/zwave/addnode` value `nop`
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
