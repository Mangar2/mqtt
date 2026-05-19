# IMPL-zwave-single-correlation-path

## Goal

Implement one single, deterministic command-reaction correlation path for Z-Wave.
The new path must fully cover both responsibilities that are currently split across two legacy paths:

1. Request tracking and node polling
2. Correct reason-chain propagation into outbound feedback messages

After completion, only one correlation path remains active. Legacy overlap must be removed.

## Why This Change

Current behavior combines two partial paths:

- Legacy Path A in Z-Wave service: service-local reply matching and reason merge.
- Legacy Path B in Z-Wave controller: pending-command tracking with polling/timeout and reason prepend.

This can produce duplicated reason chains and non-uniform correlation behavior.

## Scope

### In scope

- `src/yaha/zwave/zwave_service_component.cpp`
- `src/yaha/zwave_controller/zwave_controller.h`
- `src/yaha/zwave_controller/zwave_controller.cpp`
- `src/yaha/zwave/SPEC.md`
- `src/yaha/zwave_controller/SPEC.md`
- `src/yaha/zwave/test/TEST_SPEC.md`
- `src/yaha/zwave/test/zwave_service_component_test.cpp`
- `src/yaha/zwave_controller/test/TEST_SPEC.md`
- `src/yaha/zwave_controller/test/zwave_controller_test.cpp`

### Out of scope

- HTTP-MQTT interface behavior changes
- MQTT transport envelope format changes

## Target Behavior (Single New Path)

### 1) Track each incoming command

On each incoming `/set` command:

- Resolve target (node/class/instance/index) and expected outbound value.
- Store a pending command entry containing:
  - command topic (reply topic identity)
  - resolved target
  - expected outbound value
  - command timestamp (`sentAt`)
  - last poll timestamp (`lastPollAt`)
  - full incoming reason chain with original entry order preserved exactly
  - service hop reason `received by zwave service` inserted directly after incoming reasons
  - no reordering or sorting by timestamp

### 2) Poll only affected nodes

- In configurable intervals (`commandReactionPollIntervalMs`), poll only nodes that currently have pending entries.
- Do not poll nodes without pending entries.

### 3) Match feedback deterministically and prepend reasons

For each Z-Wave value feedback (`onValueChanged`):

- Match against pending entries using strict criteria:
  - same reply topic identity
  - same resolved target
  - same expected value (existing tolerance rules remain)
  - feedback within timeout window
- On match:
  - prepend stored reasons to outbound message reasons in original order
  - then append controller-generated reasons (for example, received-from-zwave details)

### 4) Remove pending entries on terminal conditions

- Remove entry when:
  - matched feedback was emitted, or
  - timeout exceeded (`commandReactionTimeoutMs`, default 30000 ms)

### 5) Replace identical actions, keep different actions

- If same action arrives again (same reply topic + same target + same expected value):
  - replace old pending entry with new one.
- If different feedback/value arrives while waiting:
  - keep original pending entry.

## Migration Plan

### Step A: Remove legacy service matcher path

- Remove service-local reply matcher storage and merge logic from `zwave_service_component.cpp`.
- Service keeps routing and error/log behavior only.
- Service forwards reasons in insertion order and never reorders by timestamp.

### Step B: Extend controller pending entry identity

- Extend `PendingCommand` to include reply topic identity.
- Ensure match logic uses identity + target + value + timeout.

### Step C: Keep polling/timeout loop as single authority

- Reuse existing controller polling loop and timeout cleanup.
- Ensure this loop is the only command-reaction lifecycle authority.

### Step D: Update tests/specs

- Replace service matcher tests with forwarding-only assertions.
- Add/adjust controller tests for full single-path behavior:
  - reasons prepended in correct order
  - same-action replace
  - different feedback keeps pending
  - timeout removes pending
  - poll only affected node(s)

## Acceptance Criteria

1. Only one active command-reaction correlation mechanism exists in production code.
2. No duplicated incoming reason chain in `zwave_service[out]` messages.
3. Stored incoming reasons are prepended exactly once on matched feedback.
4. Reason order is insertion-order based; timestamp-based sorting is forbidden.
5. Polling touches only nodes with pending entries.
6. Timeout and replacement semantics match requirements exactly.
7. All updated module specs and test specs match final implementation.

## Compatibility and Constraints

- No workaround behavior.
- No reduced or simplified semantics versus required behavior.
- Keep configurable interval/timeout defaults unchanged unless explicitly requested.
- Keep all files in English.

## Verification Plan

1. Run focused unit tests for `zwave_controller` and `zwave_service`.
2. Validate logs for one real command lifecycle:
  - incoming command observed
  - node polling observed
  - matched feedback publish contains prepended reasons exactly once
3. Validate mismatch case:
  - unmatched feedback does not consume pending command
  - later matching feedback still consumes and prepends reasons
4. Validate timeout case:
  - expired entry no longer prepends reasons
