# Implementation Plan: ZWave Remote Configuration and Controller State

This plan extends ZWave behavior with remote configuration and explicit controller-state visibility.

Primary goals from request:
1. Move active control commands from `$MONITOR/.../set` to `system/.../set`.
2. Reply with monitoring information that always includes current controller state.
3. Move ZWave INI configuration persistence to FileStore and reload automatically on FileStore change events.
4. Automatically append controller status and discovered elements into the FileStore configuration document.

## Scope

In scope:
- ZWave control topic migration to `system/zwave/.../set`
- controller state model and monitoring publish contract
- FileStore-backed ZWave configuration source and runtime reload
- automatic config enrichment with discovered node/status blocks
- tests, docs, deployment updates required by the above

Out of scope:
- broker protocol changes
- unrelated YAHA components
- replacing FileStore service semantics

## Referenced specifications

- [SPEC-zwave.md](./SPEC-zwave.md)
- [SPEC-filestore.md](./SPEC-filestore.md)
- [SPEC-valueservice.md](./SPEC-valueservice.md)
- [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md)

## Target behavior contract

## A. Control topic namespace migration

Canonical control topics become:
- `system/zwave/addnode/set`
- `system/zwave/removefailednode/set`
- `system/zwave/scan/set`

`$MONITOR/zwave/*/set` is no longer command namespace in final state.

## B. Monitoring replies always include controller state

Add explicit state topic contract:
- `$MONITOR/zwave/state`

State payload includes at minimum:
- `state`: current controller state string
- `detail`: short detail text
- `timestamp`: event time
- `source`: command, driver event, or reload event source

Command handling publishes:
1. command acceptance/result message (`$MONITOR/zwave/notification` or `$MONITOR/zwave/error`)
2. state snapshot message on `$MONITOR/zwave/state`

## C. FileStore as runtime config source

ZWave runtime config gains FileStore section parity with ValueService pattern:
- `enabled`
- `host`
- `port`
- `configKeyPath`
- `monitorTopicPrefix` (default `$MONITOR/FileStore`)

Startup flow:
1. Load local INI baseline.
2. If FileStore enabled, GET `configKeyPath`.
3. If FileStore content valid, replace active config with FileStore version.
4. Apply config and publish `$MONITOR/zwave/info` + `$MONITOR/zwave/state`.

Reload flow:
1. Subscribe to `<monitorTopicPrefix>/#`.
2. On matching FileStore changed/created event for `configKeyPath`, GET latest config.
3. Validate and atomically apply new config.
4. Publish reload result and updated state snapshot.

## D. Automatic status and discovered-elements enrichment

Config document persisted in FileStore keeps two blocks:
1. user-managed block (preserved exactly)
2. generated block (owned by zwave runtime)

Generated block includes:
- controller status section (last known state, last error, last update)
- discovered nodes section (node id, manufacturer, product, type, location, known classes)

Write policy:
- runtime writes generated block updates after relevant controller events
- user block is preserved byte-stable (no reformat/reorder)
- updates are debounced and deterministic

Conflict policy:
- on parse conflict, keep last valid runtime config active
- publish `$MONITOR/zwave/error` with parse detail
- do not destroy existing FileStore document

## Implementation phases

## Phase 0: Spec alignment

Step 0.1 Update [SPEC-zwave.md](./SPEC-zwave.md)
- replace management subscriptions from `$MONITOR/zwave/*/set` to `system/zwave/*/set`
- add mandatory `$MONITOR/zwave/state` publish contract
- define state enum and transition triggers
- define FileStore config-source and reload behavior

Step 0.2 Update [SPEC-valueservice.md](./SPEC-valueservice.md) references only if cross-component pattern text needs alignment.

Step 0.3 Add/update module specs:
- `src/yaha/zwave/SPEC.md`
- `src/yaha/zwave_client/SPEC.md`

Deliverable:
- all behavior changes are normative in spec before code changes

## Phase 1: Control topic migration

Step 1.1 Update `ZwaveServiceComponent::getSubscriptions()` and `handleMessage()` to use `system/zwave/.../set`.

Step 1.2 Remove command handling for old `$MONITOR/zwave/.../set` topics.

Step 1.3 Update startup marker topics from old monitor-command topics to neutral monitor info topics.

Step 1.4 Update INI template comments and README examples.

Deliverable:
- control write path uses `system/zwave/.../set` only

## Phase 2: Controller state model and monitoring responses

Step 2.1 Introduce explicit runtime state model in ZWave domain.

Initial state candidates:
- `starting`
- `driver_waiting`
- `ready`
- `scanning`
- `inclusion`
- `degraded`
- `failed`
- `stopping`

Step 2.2 Emit `$MONITOR/zwave/state` on:
- startup
- driver ready/failed
- scan begin/complete/fail
- add node begin/result
- remove failed node begin/result
- config reload begin/result

Step 2.3 Ensure every command result message includes current state snapshot publish in same handling flow.

Step 2.4 Keep monitoring and command responsibilities separated:
- commands under `system/...`
- status under `$MONITOR/...`

Deliverable:
- operator always sees current controller state via monitor topic

## Phase 3: FileStore-backed config source and reload

Step 3.1 Extend ZWave runtime config schema with optional `[filestore]` section for ZWave.

Step 3.2 Implement FileStore HTTP client helper in `zwave_client` module:
- GET config document
- POST updated config document
- bounded timeout and deterministic error text

Step 3.3 Subscribe to FileStore monitor events and filter by configured key path.

Step 3.4 On matching change event:
- fetch latest config
- parse/validate
- apply without process restart
- publish reload success/failure + state snapshot

Step 3.5 Keep local INI as bootstrap fallback when FileStore unavailable.

Deliverable:
- remote config change via FileStore triggers live ZWave reconfiguration

## Phase 4: Auto-enrichment of status and discovered nodes in FileStore

Step 4.1 Define generated block format in config document.

Step 4.2 On controller events, update in-memory generated block.

Step 4.3 Persist merged document (user block + generated block) to FileStore with debounce.

Step 4.4 Guarantee stable merge behavior:
- never rewrite user block content
- only replace generated block between explicit markers

Step 4.5 Publish monitor info when generated block persisted or persistence fails.

Deliverable:
- FileStore document always reflects current controller status and discovered nodes

## Phase 5: Tests and verification

Step 5.1 Unit tests:
- topic migration coverage (`system/...` only)
- state transition and publish contract
- FileStore reload filter and apply logic
- generated-block merge and preservation behavior

Step 5.2 Component tests:
- command in -> operation -> monitor result + state publish
- FileStore changed event -> reload -> updated subscriptions/config

Step 5.3 Runtime tests (short and bounded):
- startup with FileStore unavailable fallback
- startup with valid FileStore config override
- generated block write path success and failure logging

Step 5.4 Run targeted client-scope tests for changed modules.

Deliverable:
- deterministic proof of remote-config and state-observability behavior

## Phase 6: Deployment and operations

Step 6.1 Update `cmake/ini/zwave.ini` with `[filestore]` defaults and documentation.

Step 6.2 Update deployment defaults for ZWave service to include needed env/config values.

Step 6.3 Update operator docs with command topic migration and monitor-state topics.

Step 6.4 Add troubleshooting section for FileStore-offline and reload-parse errors.

Deliverable:
- deployable and operable remote-config setup

## Acceptance criteria

1. ZWave command subscriptions are only `system/zwave/*/set`.
2. Every command execution emits monitor feedback plus current state snapshot.
3. ZWave can bootstrap from local INI and override from FileStore when enabled.
4. FileStore change events trigger deterministic reload without restart.
5. Generated status/discovered-elements block is written automatically to FileStore.
6. User-managed config block remains unchanged during generated-block updates.
7. Errors are surfaced on `$MONITOR/zwave/error` and state is updated accordingly.

## Open decisions to confirm before implementation

1. Final canonical command prefix: `system/zwave/...` (assumed) or `system/yaha/zwave/...`.
2. Required backward-compatibility window for old `$MONITOR/zwave/*/set` topics: none (assumed) or temporary dual-subscribe.
3. FileStore key path default for ZWave config document (proposal: `/zwave/config/zwave.ini`).
4. Preferred generated-block marker syntax in INI text.
