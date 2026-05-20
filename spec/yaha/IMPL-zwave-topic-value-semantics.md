# IMPL-zwave-topic-value-semantics

## Goal

Ensure every Z-Wave outbound message is semantically complete in `topic + value` alone.
The `reason` field remains trace-only and must not carry required business meaning.

Primary design rule:
- A topic is a state variable (resource path), not a generic message sink.
- The value is the current state/value of that one resource.

## Source Scope

- `src/yaha/zwave/zwave_service_component.cpp`
- `src/yaha/zwave_controller/zwave_controller.cpp`
- `src/yaha/zwave/SPEC.md`
- `src/yaha/zwave_controller/SPEC.md`

## Problem Statement

Current Z-Wave message generation still emits notifications where `topic + value` is ambiguous or content-poor.
Examples observed in runtime logs include `topic="$MONITOR/zwave/notification" value="nop"`.

## Inventory of Semantically Invalid Messages (Current State)

1. Current: `topic=$MONITOR/zwave/notification`, `value=nop`
- Source: `ZwaveController::onNotification()` via `notificationText(ZwaveNotificationCode::Nop)`
- Why invalid: `notification nop` is not a complete business statement.

2. Current: `topic=$MONITOR/zwave/notification`, `value=timeout`
- Source: `ZwaveController::onNotification()` via `notificationText(ZwaveNotificationCode::Timeout)`
- Why invalid: no affected object/context in `topic + value`.

3. Current: `topic=$MONITOR/zwave/notification`, `value=message completed`
- Source: `ZwaveController::onNotification()` via `notificationText(ZwaveNotificationCode::MessageComplete)`
- Why invalid: completion of what is not encoded in `topic + value`.

4. Current: `topic=$MONITOR/zwave/notification`, `value=node awake`
- Source: `ZwaveController::onNotification()` via `notificationText(ZwaveNotificationCode::NodeAwake)`
- Why invalid: missing node identity in `topic + value`.

5. Current: `topic=$MONITOR/zwave/notification`, `value=node sleep`
- Source: `ZwaveController::onNotification()` via `notificationText(ZwaveNotificationCode::NodeSleep)`
- Why invalid: missing node identity in `topic + value`.

6. Current: `topic=$MONITOR/zwave/notification`, `value=node dead`
- Source: `ZwaveController::onNotification()` via `notificationText(ZwaveNotificationCode::NodeDead)`
- Why invalid: missing node identity in `topic + value`.

7. Current: `topic=$MONITOR/zwave/notification`, `value=node alive`
- Source: `ZwaveController::onNotification()` via `notificationText(ZwaveNotificationCode::NodeAlive)`
- Why invalid: missing node identity in `topic + value`.

8. Current: `topic=$MONITOR/zwave/notification`, `value=unknown`
- Source: `ZwaveController::notificationText(default)`
- Why invalid: no semantic payload.

9. Current: `topic=$MONITOR/zwave/error`, `value=<same as notification text>`
- Source: fallback branch in `ZwaveController::onNotification()`
- Why invalid: some values (`unknown`, `nop`, `message completed`) are not error-complete statements.

10. Current: `topic=$MONITOR/zwave/<nodeId>`, `value=<raw OpenZWave value>`
- Source: fallback in `ZwaveController::publishValue()`
- Why invalid: value meaning is ambiguous without class/index/type in `topic + value`.

## Target Message Semantics

1. Management and service state topics stay in `system/zwave/...`.
2. Monitoring-only diagnostics remain under `$MONITOR/...`.
3. Every notification/error payload must carry complete business meaning in `topic + value`.
4. Node-scoped events must encode node identity in the topic path, not only in `reason`.
5. Unknown/fallback messages must use explicit machine-meaningful value vocabularies.
6. Events without operational value for automation (for example `nop`) are not published as state variables.

## Planned Topic Model Changes

1. Replace generic notification sink with state resources.

1.1 Node health state
- from: `$MONITOR/zwave/notification` values `node alive|node dead`
- to: `$MONITOR/zwave/node/<nodeId>/health`
- value set: `alive`, `dead`

1.2 Node power/activity state
- from: `$MONITOR/zwave/notification` values `node awake|node sleep`
- to: `$MONITOR/zwave/node/<nodeId>/power_state`
- value set: `awake`, `sleep`

1.3 Communication timeout state
- from: `$MONITOR/zwave/notification` value `timeout`
- to: `$MONITOR/zwave/node/<nodeId>/comm/state`
- value set: `ok`, `timeout`
- state-transition rule:
  - set `timeout` on timeout notification
  - set back to `ok` on next successful communication callback for same node
- rationale: this is an actionable variable, not an ever-growing metric.

1.4 No-op and message-complete handling
- `nop` and `message completed` are not published as standalone resource states.
- default behavior: suppress publish, keep only trace log.

1.5 Unknown notification handling
- unknown notifications are not published as normal state values.
- default behavior: suppress publish, keep only trace log.
- escalation rule: publish only when the implementation can classify a concrete actionable error state.

2. Replace generic notification-error fallback:
- from: `$MONITOR/zwave/error` with ambiguous value
- to: `$MONITOR/zwave/node/<nodeId>/error/state`
- value set: `no_error`, `publish_failed`, `decode_failed`, `driver_failed`
- priority rule (must not be overwritten by lower severity):
  - `driver_failed` > `decode_failed` > `publish_failed` > `no_error`
- overwrite rule:
  - higher severity may overwrite lower severity
  - lower severity must never overwrite higher severity
  - example: `publish_failed` must not overwrite existing `driver_failed`
- recovery rule:
  - controller publishes `no_error` only on explicit recovery condition
  - `no_error` is not published periodically and does not clear active higher-severity errors implicitly

3. Replace unmapped value fallback:
- from: `$MONITOR/zwave/<nodeId>`
- to: `$MONITOR/zwave/node/<nodeId>/class/<classId>/instance/<instance>/index/<index>/value/unmapped`
- value: current measured value at this concrete unmapped resource path (number/string/bool->normalized)
- semantic rule:
  - topic identifies exactly which resource produced the value
  - value is the current state of that one resource

4. Scan state vocabulary
- use explicit scan state variable:
  - topic: `$MONITOR/zwave/scan/state`
  - value set: `scanning`, `idle`
- use explicit scan result variable:
  - topic: `$MONITOR/zwave/scan/result`
  - value set: `scanning_completed`, `scanning_failed`

## Planned Code Changes

1. `src/yaha/zwave_controller/zwave_controller.cpp`
- Replace `notificationText()` return vocabulary to normalized machine values.
- Change `onNotification()` publish target to node-scoped state topics.
- Change fallback error publish to node-scoped error topics.
- Enforce error-state priority and guarded overwrite behavior.
- Change `publishValue()` fallback topic to `.../node/<id>/class/<classId>/instance/<instance>/index/<index>/value/unmapped`.
- Suppress `nop`, `message completed`, and non-actionable unknown notifications from state publication path.
- Keep `reason` as trace-only, remove required semantic dependency on reason text.

2. `src/yaha/zwave/zwave_service_component.cpp`
- Keep management state on `system/zwave/...`.
- Keep pass-through behavior for controller publishes.
- Ensure no service-generated message relies on reason for primary meaning.

## Planned Spec Updates

1. Update `src/yaha/zwave_controller/SPEC.md`
- Replace current `$MONITOR/zwave/notification` generic contract with node-scoped state resources.
- Define canonical value vocabulary and state-transition rules per resource.
- Define error-state priority, overwrite, and recovery semantics.
- Define unmapped fallback topic shape including class/instance/index resource identity.

2. Update `src/yaha/zwave/SPEC.md`
- Clarify separation:
  - `system/zwave/...` for operational command/state interface.
  - `$MONITOR/zwave/...` for diagnostics/monitoring only.
- Add explicit rule: semantic completeness is mandatory in `topic + value`.

3. Update `spec/yaha/SPEC-zwave.md`
- Align high-level YAHA spec with refined topic taxonomy and vocabulary.
- Add rule: non-actionable events are suppressed; only actionable state variables are published.

## Test Plan

1. `src/yaha/zwave_controller/test/zwave_controller_test.cpp`
- Replace assertions expecting generic `$MONITOR/zwave/notification` with node-scoped state topics.
- Add one test per listed previously invalid message ensuring meaningful `topic + value`.
- Add tests that `nop`, `message completed`, and unknown notifications do not produce meaningless state publishes.
- Add tests proving lower-severity errors cannot overwrite higher-severity error state.
- Add tests proving explicit recovery transitions error state to `no_error`.
- Add tests for unmapped fallback topic shape with class/instance/index identity.

2. `src/yaha/zwave/test/zwave_service_component_test.cpp`
- Validate pass-through still preserves controller topic/value semantics.
- Validate service `system/zwave` management status topics remain unchanged.

3. Execute client test script:
- `python3 test/run_coverage_clients.py`

## Acceptance Criteria

1. No runtime message of form `$MONITOR/zwave/notification` with value `nop`, `unknown`, or other ambiguous values remains.
2. Node-related event semantics are fully derivable from `topic + value` without reading `reason`.
3. Unmapped fallback values publish to explicit fallback topic family (`.../value/unmapped`).
4. Existing `system/zwave` command/status interface remains stable.
5. Updated specs and tests document and enforce the new contract.
6. `nop`, `message completed`, and unknown notifications are suppressed unless mapped to explicit actionable states.
7. Error-state priority is preserved: lower-severity states never overwrite higher-severity states.
8. Error state supports explicit healthy value `no_error` with deterministic recovery transitions.
