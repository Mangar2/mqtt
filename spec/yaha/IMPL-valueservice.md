# Implementation Plan: ValueService 1.0

This plan defines the implementation sequence for
[SPEC-valueservice.md](./SPEC-valueservice.md).

The scope is a new YAHA ValueService client component with FileStore-backed
load/save behavior aligned with Automation client patterns.

## Goal

Implement one coherent ValueService standalone client that:
- keeps a topic-keyed value map in memory
- handles `<topic>/set` commands and publishes retained state updates
- loads and persists the full map through FileStore HTTP APIs
- reloads values when FileStore monitoring reports matching key-path changes

## Scope

In scope:
- ValueService domain component implementing `IMqttComponent`
- FileStore HTTP integration (GET/POST full map)
- FileStore monitor topic handling
- standalone runtime config mapping and executable composition
- focused unit tests and component-level integration tests

Out of scope:
- FileStore server implementation changes
- broker runtime changes
- non-ValueService YAHA components

## Referenced specifications

- [SPEC-valueservice.md](./SPEC-valueservice.md)
- [SPEC-filestore.md](./SPEC-filestore.md)
- [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md)
- [SPEC-message.md](./SPEC-message.md)

## Target module structure

Create two YAHA modules.

1. Domain module: `src/yaha/value_service/`
- ValueService config type
- ValueService component implementation (`IMqttComponent`)
- Value-map validation helpers
- FileStore monitor payload parser helper

2. Runtime module: `src/yaha/value_service_client/`
- INI runtime config loader
- mapping from `[filestore]`, `[valueservice]`, `[monitoring]`, `[mqtt]`
- runtime composition helpers for standalone process

Standalone entry point:
- `src/yaha_valueserviceclient_main.cpp`

## Implementation phases

## Phase 1: Contracts and config mapping

Step 1. Define ValueService config contract
- add config struct fields for qos, monitor prefix, file-store settings, key path
- set defaults from spec
- reject invalid qos/port/bool values in parser paths

Step 2. Implement runtime INI mapping
- parse component and filestore keys from INI
- provide one loader for domain config and one for full runtime config
- keep error messages field-specific and deterministic

Deliverable:
- valid runtime config object for ValueService + MQTT runtime

## Phase 2: Domain component behavior

Step 3. Implement in-memory value map and subscription projection
- hold map `key -> value`
- compute subscriptions for `<key>/set`
- include monitor prefix `/<#>` subscription

Step 4. Implement startup load + replay
- on `run()` call FileStore GET for configured key path
- validate loaded map (`string | integer` values only)
- replace in-memory map when valid
- publish retained replay for all keys through callback

Step 5. Implement `/set` message handling
- detect topics ending in `/set`
- map to key without suffix
- update map and return one retained state message (`<key>`)
- persist full map to FileStore via POST after accepted update

Step 6. Implement monitor-triggered reload
- detect monitor events under configured prefix
- parse payload and verify `keyPath == valuesKeyPath`
- on matching change event, GET current map from FileStore
- replace map and publish retained replay

Deliverable:
- complete ValueService behavior parity plus mandatory FileStore persistence

## Phase 3: Standalone composition

Step 7. Compose ValueService client runtime
- create ValueService component from parsed config
- wire publish callback to MQTT runtime publish port
- execute startup `run()` before entering MQTT loop
- route incoming MQTT messages into `handleMessage`

Step 8. Add standalone main
- add new executable main for ValueService client
- load INI config
- construct runtime and run until signal shutdown
- ensure clean `close()` path

Deliverable:
- runnable `yahavalueserviceclient` process

## Phase 4: Verification

Step 9. Unit tests
- config parser success/failure paths
- map validation rules for loaded payload
- `/set` handling output message shape (topic/value/qos/retain)
- monitor event parsing and key-path matching

Step 10. Component tests with mock FileStore HTTP server
- startup load from FileStore
- `/set` update posts full map
- POST failure does not suppress outbound retained state publish
- monitor changed event reloads and replays map
- non-matching monitor event does not reload

Step 11. Runtime composition tests
- full runtime config mapping into domain+mqtt config
- callback wiring publishes returned component messages

Deliverable:
- deterministic behavior verified against spec contract

## Acceptance criteria

1. Startup loads values from FileStore key path when enabled.
2. Every accepted `/set` update publishes retained state and posts full map to FileStore.
3. Monitor event with matching key path reloads values from FileStore.
4. Invalid FileStore data never replaces valid in-memory state.
5. Runtime config supports all documented defaults and validations.
6. New standalone ValueService client target is build-integrated and test-covered.

## Risks and implementation notes

1. Reload race during concurrent set updates
- apply one serialized update path (single mutex/strand) for map replace and map write.

2. FileStore payload shape drift
- keep strict map validation and fail closed on invalid payload.

3. Reload storm from monitor events
- optional short debounce window can be added if event volume is high.

4. Legacy migration behavior
- keep legacy `valuesFileName` parser compatibility only if needed for migration,
  but do not read local files in runtime execution path.

## Step execution order summary

Step 1 -> Step 2 -> Step 3 -> Step 4 -> Step 5 -> Step 6 -> Step 7 -> Step 8 -> Step 9 -> Step 10 -> Step 11
