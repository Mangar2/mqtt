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
- `requestNodeInfo(...)`
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
- `requestNodeInfo(value)`
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
- `onNodeRemoved(...)`
- `onValueAdded(...)`
- `onValueRemoved(...)`
- `onValueChanged(...)`
- `onValueRefreshed(...)`

## Behavior

## Internal component split

- `zwave_controller_value_utils.*` encapsulates value/boolean conversion,
  semantic value-equivalence checks, debug-value rendering, and YAHA
  reason-entry sanitization/timestamp compliance handling.
- `zwave_controller_topic_utils.*` encapsulates set-topic parsing/joining and
  node-id extraction helpers for command routing.
- `zwave_controller_reason_utils.*` encapsulates callback reason-text builders
  and allowlisted command-class checks for polling behavior.

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
- `requestNodeInfo(value)` parses numeric node id and routes to `driver.requestNodeInfo(...)`
- `close()` disconnects via configured USB device path

## Event and publish contract

- Driver lifecycle:
  - `onDriverReady` publishes `system/zwave/scan` value `scanning`
  - `onDriverFailed` publishes `$MONITOR/zwave/driver/error/state` value `driver_failed`
  - `onDriverFailed` publishes `system/zwave/scan` value `failed`
  - controller tracks timeout-storm watchdog state for process self-termination integration:
    - counts only `Timeout` notifications (`openzwave_notification_timeout`) as timeout-drop errors
    - resets watchdog counters on successful inbound events (`onValueAdded`, `onValueChanged`, `onValueRefreshed`, `NodeAlive`, `NodeAwake`)
    - triggers unresponsive-network callback once when both are true since last successful input:
      - at least `100` timeout-drop errors
      - at least `180000ms` elapsed without successful inbound input
  - `onScanComplete` publishes `system/zwave/scan` value `off`
- Notification callback is resource-oriented:
  - all node-scoped state topics use mapped device paths: `$MONITOR/<device-topic>/<state>`
  - `Timeout` -> `$MONITOR/<device-topic>/comm/state` value `timeout`
  - `NodeAwake` -> `$MONITOR/<device-topic>/power_state` value `awake`
  - `NodeSleep` -> `$MONITOR/<device-topic>/power_state` value `sleep`
  - `NodeDead` -> `$MONITOR/<device-topic>/health` value `dead` plus `comm/state=timeout`
  - `NodeAlive` -> `$MONITOR/<device-topic>/health` value `alive`
  - `MessageComplete` and `Nop` are suppressed
- Configuration capability discovery:
  - on class `0x70` value discovery (`onValueAdded`/`onValueChanged`), controller publishes one capability snapshot per node+instance+parameter index
  - capability topics: `$MONITOR/<device-topic>/config/param/<id>/supported`, `/type`, `/read_only`, and optional `/label`
  - capability publish is deduplicated per discovered parameter key to avoid repeated metadata spam
- Health state machine:
  - internal initial state is `unknown` and is never published
  - no health publish on `onNodeReady` alone
  - publish `alive` when first real node information arrives (`onValueAdded`, `onValueChanged`, `onValueRefreshed`) or when transitioning from `dead`
  - publish `dead` on `NodeDead` notification
- Communication state machine:
  - default state is `ok` and is published only on state transitions
  - `Timeout` or `NodeDead` transitions to `comm/state=timeout`
  - `Timeout` transition reason includes source context marker `source=openzwave_notification_timeout` and context detail (`context=pending_command ...` or `context=no_pending_command`)
  - only successful value callbacks (`onValueChanged`, `onValueRefreshed`) transition to `comm/state=ok`
  - `comm/state=ok` reason includes evidence details: source (`openzwave_value_changed` or `openzwave_value_refreshed`) and target `node/class/instance/index` plus `valueId`
- Notification publish failures are reported as node-scoped error state:
  - `$MONITOR/<device-topic>/error/state` value `publish_failed`
  - severity order is enforced (`publish_failed` < `decode_failed` < `driver_failed`)
  - lower severity never overwrites higher severity
  - successful communication clears to `no_error`
- Controller command callback publishes to `$MONITOR/zwave/controller/command/last_status`.
- Node include process monitoring publishes to `$MONITOR/zwave/node/<nodeId>/include` only for include-flow completion:
  - `onNodeAdded` marks the node as include-flow candidate without publishing include state
  - `onNodeReady(..., "queries_complete")` enables value polling only for allowlisted switch classes `0x25` and `0x26`
  - when values are discovered after interview completion (`onValueAdded`/`onValueChanged` on ready node), controller re-triggers polling only if the discovered class id is in the same allowlist (`0x25`, `0x26`)
  - `onNodeReady(..., "queries_complete")` publishes `included` only when the node was previously marked by `onNodeAdded`
- Node/value callbacks maintain in-memory node/class cache.
- `onNodeRemoved` clears runtime state for that node from node/value cache, node-monitor state caches, pending-command tracking, include-flow tracking, and discovered config-capability keys.
- Controller keeps local runtime state in unordered maps:
  - node runtime map keyed by `nodeId` with `ready/dead` status and latest value events per class/index
  - topic state map keyed by mapped MQTT topic with latest locally observed outbound value and optional zwave network value id
- `onValueRemoved` prunes empty class containers after index removal so repeated remove/add cycles do not retain unneeded empty nested maps.
- `onValueChanged` updates cache and publishes mapped value.
- `onValueRefreshed` updates cache and always publishes mapped outbound value (including non-command refreshes).
- Pending command feedback behavior:
  - controller runs a background poll loop and requests `driver.requestNodeState(nodeId)` per pending command on `commandReactionPollIntervalMs`
  - controller also runs a full-device poll loop and requests `driver.requestNodeState(nodeId)` for all configured node ids on `pollIntervalMs`
  - if a value-changed/value-refreshed event matches pending reply topic + target + expected value and is still within timeout, the pending command is consumed
  - correlation key is always the resolved target address tuple (`nodeId/classId/instance/index`) plus expected value; it is never based on runtime ValueID ids
  - for multilevel class (`0x26`) feedback, pending expected bool values accept non-zero numeric state as `on` and zero as `off`
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
  - mapping failures fall back to `$MONITOR/<device-topic>/class/<classId>/instance/<instance>/index/<index>/value/unmapped`
  - when no device-topic mapping is available, fallback uses `$MONITOR/zwave/node/<nodeId>/class/<classId>/instance/<instance>/index/<index>/value/unmapped`
  - value/event reasons include actionable context `received from zwave network node: <nodeId>` and append network id when available

## Files

- `zwave_controller.h`
- `zwave_controller.cpp`
- `zwave_controller_value_utils.h`
- `zwave_controller_value_utils.cpp`
- `zwave_controller_topic_utils.h`
- `zwave_controller_topic_utils.cpp`
- `zwave_controller_reason_utils.h`
- `zwave_controller_reason_utils.cpp`
- `test/TEST_SPEC.md`
- `test/zwave_controller_test.cpp`
