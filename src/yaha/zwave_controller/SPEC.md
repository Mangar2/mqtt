# zwave_controller — YAHA ZWave Controller Adapter Behavior

## Purpose

Provides a controller adapter layer that translates ZWave callback/events and
MQTT `/set` requests into deterministic publish operations and driver-port calls.

## Public API

### enum ZwaveNotificationCode

Maps notification ids to parity notification text:
- `MessageComplete` (0)
- `Timeout` (1)
- `Nop` (2)
- `NodeAwake` (3)
- `NodeSleep` (4)
- `NodeDead` (5)
- `NodeAlive` (6)

### struct ZwaveControllerValueEvent

Incoming value callback payload:
- `nodeId`, `classId`, `instance`, `index`
- optional `label`, optional `valueId`
- `value`, `type`, `readOnly`

### interface IZwaveDriverPort

Driver boundary used by the adapter:
- `setValue(...)`
- `setConfigParam(...)`
- `addNode()`
- `removeFailedNode(...)`
- `startScan()`
- `requestAllConfigParams(...)`
- `enablePoll(...)`
- `disconnect(...)`

### interface IZwaveController

Service-facing controller boundary:
- `setPublishCallback(...)`
- `setDeviceConfiguration(...)`
- `setValue(topic, value)`
- `addDevice()`
- `removeFailedNode(value)`
- `startScan()`
- `requestConfigParametersForAllNodes()`
- `close()`

### class ZwaveController

Concrete parity adapter implementation with additional callback entry points:
- `onDriverReady(...)`
- `onDriverFailed()`
- `onScanComplete()`
- `onNotification(...)`
- `onControllerCommand(...)`
- `onNodeAdded(...)`
- `onNodeReady(...)`
- `onValueAdded(...)`
- `onValueRemoved(...)`
- `onValueChanged(...)`
- `onValueRefreshed(...)`

## Behavior

## Inbound set routing

`setValue(topic, value)` behavior:
- requires trailing `/set`
- resolves ZWave id through `ZwaveDevicesMapper::topicToZwaveId(...)`
  - first tries direct device topic: incoming topic without trailing `/set`
  - falls back to legacy label/topic split when direct mapping is unavailable
- converts payload via `ZwaveDevicesMapper::buildWriteRequest(...)`
- for regular `setValue` writes, stores a pending command entry with:
  - reply topic identity (incoming topic without trailing `/set`)
  - resolved target (`nodeId`, `classId`, `instance`, `index`)
  - expected outbound value
  - command timestamp
  - incoming message reason list
- routes:
  - `SetConfigParam` -> `driver.setConfigParam(...)`
  - `SetValue` -> `driver.setValue(...)`

## Controller operations

- `addDevice()` -> `driver.addNode()`
- `removeFailedNode(value)` parses numeric node id and routes to `driver.removeFailedNode(...)`
- `startScan()` routes to `driver.startScan()` and publishes deterministic success message
- `requestConfigParametersForAllNodes()` iterates configured node ids and requests config params
- `close()` disconnects via configured USB device path

## Event and publish contract

- Driver lifecycle:
  - `onDriverReady` publishes `$MONITOR/zwave/notification` value `starting scan`
  - `onDriverFailed` publishes `$MONITOR/zwave/error` value `driver failure`
  - `onScanComplete` publishes `$MONITOR/zwave/notification` value `scan complete`
- Notification callback publishes only to `$MONITOR/zwave/notification` (never to device topics); on failures publishes to `$MONITOR/zwave/error`.
- Controller command callback publishes to `$MONITOR/zwave/notification`.
- Node/value callbacks maintain in-memory node/class cache.
- Controller keeps local runtime state in unordered maps:
  - node runtime map keyed by `nodeId` with `ready/dead` status and latest value events per class/index
  - topic state map keyed by mapped MQTT topic with latest locally observed outbound value and optional zwave network value id
- `onValueChanged` updates cache and publishes mapped value.
- `onValueRefreshed` updates cache and publishes outbound mapped value only when it matches a pending command.
- Pending command feedback behavior:
  - controller runs a background poll loop and requests `driver.requestNodeState(nodeId)` per pending command on `commandReactionPollIntervalMs`
  - if a value-changed/value-refreshed event matches pending reply topic + target + expected value and is still within timeout, the pending command is consumed
  - expected value comparison accepts semantic bool equivalence across representations (`on/true/1`, `off/false/0`)
  - consumed pending command reasons are prepended to outbound message reasons in original order
  - prepended reasons are sanitized for YAHA Message conformance: empty reason messages are dropped; invalid reason timestamps are replaced with freshly generated ISO-8601 UTC timestamps
  - pending command entries expire after `commandReactionTimeoutMs`
  - on timeout, controller publishes one feedback message on the pending reply topic using only locally cached topic state (no device fetch)
  - timeout feedback keeps the original command reasons prepended and appends reason `timeout waiting for zwave network id: <id>` (`unknown` when id is unavailable)
  - same action (same reply topic + same target + same expected value) replaces previous pending entry
  - different action on same target is kept as independent pending entry
- Value publish behavior:
  - node `1` publishes to configured USB topic
  - mapped devices publish via `valueToTopicAndType`
  - `switch` type converts bool to `on`/`off`
  - mapping failures fall back to `$MONITOR/zwave/<nodeId>`

## Files

- `zwave_controller.h`
- `zwave_controller.cpp`
- `test/TEST_SPEC.md`
- `test/zwave_controller_test.cpp`
