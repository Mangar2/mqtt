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
- derives label/topic using legacy split order
- resolves ZWave id through `ZwaveDevicesMapper::topicToZwaveId(...)`
- converts payload via `ZwaveDevicesMapper::buildWriteRequest(...)`
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
  - `onDriverReady` publishes `$MONITORING/zwave/notification` value `starting scan`
  - `onDriverFailed` publishes `$MONITORING/zwave/error` value `driver failure`
  - `onScanComplete` publishes `$MONITORING/zwave/notification` value `scan complete`
- Notification callback publishes mapped notification text; on failures publishes to `$MONITORING/zwave/error`.
- Controller command callback publishes to `$MONITORING/zwave/notification`.
- Node/value callbacks maintain in-memory node/class cache.
- `onValueChanged` updates cache and publishes mapped value.
- Value publish behavior:
  - node `1` publishes to configured USB topic
  - mapped devices publish via `valueToTopicAndType`
  - `switch` type converts bool to `on`/`off`
  - mapping failures fall back to `$MONITORING/zwave/<nodeId>`

## Files

- `zwave_controller.h`
- `zwave_controller.cpp`
