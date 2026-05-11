# ZWave

## Purpose

ZWave is a YAHA component that bridges MQTT topics to Z-Wave network operations and maps Z-Wave value/notification events back to MQTT messages.

It encapsulates OpenZWave driver lifecycle, node/value event handling, topic-to-value addressing, and command execution for configured Z-Wave devices.

## Role in the system

ZWave is a domain component behind the YAHA runtime boundary from [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md).

It consumes:
- inbound MQTT set/control topics
- OpenZWave driver and node/value events
- static device mapping configuration

It produces:
- outbound MQTT notifications about driver/network events
- outbound MQTT state/value messages derived from Z-Wave values

## Standalone program structure

Standalone ZWave executable contains:
1. Generic YAHA MQTT client runtime.
2. ZWave domain component (`ZwaveService` equivalent behavior).
3. Z-Wave controller adapter around OpenZWave driver.
4. Device mapping resolver (`topic -> zwave id`, `zwave value -> topic`).

Main composition rule:
- main composes component and MQTT runtime.
- runtime owns broker lifecycle.
- ZWave component owns Z-Wave domain behavior and mapping.

## Subscriptions

`getSubscriptions()` returns map `topicPattern -> qos`.

Mandatory subscriptions:
1. Management topics:
- `$MONITORING/zwave/removefailednode/set` with qos `2`
- `$MONITORING/zwave/addnode/set` with qos `2`

2. Device set topics derived from `devices` configuration:
- if `class_id` is defined for device entry: `<device.topic>/set`
- if `class_id` is undefined for device entry: `<device.topic>/+/set`
- qos = configured `subscribeQoS`

Compatibility note:
- Legacy source uses `$SYS/zwave/...` control topics.
- YAHA spec maps these to `$MONITORING/zwave/...`.

Normative topic rule:
- Runtime uses only `$MONITORING/...` topics.
- No `$SYS/...` runtime alias subscribe/publish behavior is required.

## Published messages

ZWave publishes the following message categories:

1. Driver and scan lifecycle notifications:
- `$MONITORING/zwave/notification` with values such as `starting scan`, `scan complete`, controller command feedback.

2. Error notifications:
- `$MONITORING/zwave/error` for driver start failure and notification handling errors.

3. Service info notifications:
- `$MONITORING/zwave/info` value `configuration reloaded` when device configuration is applied.
- `$MONITORING/zwave/removefailednode` and `$MONITORING/zwave/addnode` with value `nop` during startup run.

4. Device value updates:
- topic from `valueToTopicAndType` mapping for regular nodes
- USB/controller topic for node `1`
- fallback topic `$MONITORING/zwave/<nodeId>` on mapping failure

Publish metadata rules:
- every published message is passed through reply matcher update (`MatchMessages` behavior)
- qos is always configured `qos`
- retain is always configured `retain`

## External interfaces

Public component interface:
- `run()`
- `close()`
- `getSubscriptions() -> map`
- `handleMessage(message)`
- `on("publish", callback)`
- `setDeviceConfiguration(config)`

Controller operations exposed to component:
- set value by topic path (`.../set`)
- add node
- remove failed node
- request all config params for all configured nodes
- close/disconnect

## Data model

## Configuration shape

Required top-level keys:
- `subscribeQoS: 0|1|2`
- `qos: 0|1|2`
- `usb: { device: string, topic: string }`
- `devices: array`

Optional key:
- `retain: boolean` (default `false`)

Device entry minimum fields:
- `topic`
- `node_id`

Optional matching fields:
- `class_id`, `instance`, `index`, `type`, `label`

## Device mapping behavior

`valueToTopicAndType(zwaveValue)`:
- finds best matching device config by node and optional class/instance/index precision scoring
- if mapping has no class_id and incoming value has label, label is appended to topic
- caches mapping by `value_id` when present

`topicToZwaveId(nodes, topic, label)`:
- first tries `<topic>/<label>`, then `<topic>` mapping
- completes defaults:
  - instance default `1`
  - index default `0`
  - type default `bool`
- if class_id missing, resolves by searching node class tree by label and instance

## Value conversion behavior

For outgoing MQTT value messages:
- if mapped type is `switch`, boolean value is converted to `on` or `off`
- otherwise raw OpenZWave value is published

For incoming set commands:
- bool/switch target types treat `on`, `1`, `1`, `true`, `"true"` as true
- byte target type converts input via numeric conversion
- config-class values (`class_id == 0x70`) use `setConfigParam`

## Behavior

## Startup

On component construction:
1. sanitize configuration with defaults.
2. create controller and connect OpenZWave to configured USB device.
3. register controller publish callback and forward through matcher + qos/retain.

On `setDeviceConfiguration(config)`:
1. apply device configuration to controller mapper.
2. publish `$MONITORING/zwave/info` (`configuration reloaded`).

On `run()`:
1. publish restart marker messages for removefailednode/addnode topics with `nop`.
2. request all configuration parameters for all configured nodes.

## OpenZWave event handling

Controller behavior handles events and publishes mapped messages:
- `driver ready`: publish start scan notification with home id context.
- `driver failed`: publish error notification.
- `scan complete`: publish scan complete notification.
- `notification`: publish mapped notification text per code table.
- `controller command`: publish command feedback notification.
- `node added`: create node structure.
- `node ready`: copy node metadata and enable polling for classes `0x25` and `0x26`.
- `value added`: update node class cache only (no publish).
- `value removed`: remove from node class cache.
- `value changed`: update cache and publish mapped value update.
- `value refreshed`: log only.

## Inbound MQTT handling

For each incoming message:
1. if topic is `$MONITORING/zwave/removefailednode/set`: invoke remove-failed-node operation.
2. else if topic is `$MONITORING/zwave/addnode/set`: invoke add-node operation.
3. else if topic is `$MONITORING/zwave/scan/set`: invoke scan operation.
4. else:
- add reason `received by zwave service`
- register message in reply matcher
- execute value set routing by topic mapping

Compatibility note for scan command:
- legacy service routes scan topic to `startScan()`.
- legacy controller implementation has no `startScan` method.

Normative YAHA decision:
- New C++ client implements a defined scan operation for `$MONITORING/zwave/scan/set`.
- Scan request must trigger controller scan/start-inclusion flow and publish deterministic success/failure notification messages.
- Legacy no-op/error behavior for scan is intentionally not preserved.

## Persistence

No durable component persistence is defined.

Runtime state only:
- node registry and class value cache
- value-id-to-topic cache
- message matching cache
- OpenZWave runtime/driver state

## Configuration

Defaults from sanitize path:
- `subscribeQoS = 1`
- `qos = 1`
- `retain = false`

Validation rules from schema:
- unknown top-level properties are rejected
- required fields must exist
- QoS must be one of `0|1|2`
- usb requires both `device` and `topic`
- each device entry must include `topic` and `node_id`

## Error handling

- mapping or set errors are caught and logged via error log path.
- notification mapping errors publish to `$MONITORING/zwave/error` and log details.
- missing mapping during value publish falls back to `$MONITORING/zwave/<nodeId>` and logs context.
- component continues runtime after recoverable per-message failures.

## Architectural notes

- Domain behavior is split between service orchestration, controller adapter, and devices mapping helper.
- publish callback is the only outbound MQTT boundary from the domain component.
- backend stack is an OpenZWave-compatible adapter behind controller abstraction.
- behavior parity is mandatory for functional contracts, except explicitly approved deviations in this spec.

## ZWave stack and delivery model

Normative stack decision:
- The new C++ client uses OpenZWave as embedded Z-Wave backend.
- OpenZWave is compiled into the client build and shipped together with the client artifacts.
- Runtime must not depend on a separately installed system OpenZWave package.

Normative source-management decision:
- OpenZWave source code is stored locally at `src/third_party/openzwave`.
- Canonical upstream source is `https://github.com/OpenZWave/open-zwave.git`.
- OpenZWave version is pinned to an explicit full commit hash in `src/third_party/openzwave/PINNED_VERSION.txt`.
- Third-party origin/license notes are tracked in `spec/third_party/openzwave/NOTICE.md`.
- Local OpenZWave maintenance changes are tracked in `spec/third_party/openzwave/CHANGELOG.md`.
- Pinned version changes are explicit maintenance changes and must pass full parity and integration tests.

Normative architecture constraint:
- Even with embedded OpenZWave, controller access remains behind an adapter boundary.
- Domain/service logic must not call OpenZWave APIs directly.

## Mandatory parity requirement for new C++ client

This section is normative and release-blocking.

The new C++ ZWave client must preserve functional behavior defined by legacy executable behavior from:
- `zwaveservice.js`
- `zwavecontroller.js`
- `zwavedevices.js`

No functional deviation is allowed in:
- subscription derivation
- topic routing and command dispatch
- mapping resolution and default completion rules
- conversion rules for bool/switch/byte values
- published topics, payload transforms, and reason handling
- event-to-message mapping
- fallback/error-path behavior

Explicitly approved deviation:
- scan command behavior is fixed to a real scan operation (see Inbound MQTT handling section).

Allowed tolerance for all non-approved areas is zero.

## Mandatory unit-test gate for parity

Development is incomplete without a gapless unit-test suite proving functional parity against legacy behavior plus explicit verification of approved scan fix behavior.

Required gate:
1. Mapping matrix tests for `valueToTopicAndType` and `topicToZwaveId` including tie-break and default rules.
2. Event contract tests for all OpenZWave callback event handlers and resulting publish outputs.
3. Command routing tests for all handled control and set topic paths.
4. Conversion tests for bool/switch/byte and config-parameter path.
5. Golden-reference replay tests comparing C++ output stream with legacy JS output stream for identical synthetic event/message sequences.
6. Dedicated scan-command tests proving new defined scan behavior (success/failure publish contract and controller call path).
7. Negative assertion: any differing field at any replay step fails test run for non-approved deviation areas.

Acceptance rule:
- single-step mismatch rejects implementation.
- no waiver for parity differences.
