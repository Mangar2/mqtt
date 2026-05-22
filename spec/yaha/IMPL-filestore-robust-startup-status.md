# Implementation Plan: Robust FileStore Startup and Status Lifecycle for Automation, ValueService, and ZWave

This plan defines the implementation sequence for robust startup, retry, and lifecycle status signaling in three YAHA services that use FileStore:
- automation
- valueservice
- zwave

The plan standardizes startup order, status topic behavior, FileStore retry handling, and shutdown signaling across all three services.

## Goal

Implement one uniform lifecycle contract across automation, valueservice, and zwave:
1. connect to MQTT broker first
2. publish status `starting`
3. load required data from FileStore
4. publish status `running` only after successful FileStore load
5. retry FileStore load with configurable attempts and interval
6. terminate process after retry budget is exhausted
7. publish status `stopped` on graceful self-termination and signal-driven shutdown
8. set MQTT Last Will status `terminated` after broker connection, so unexpected disconnects are visible

## Scope

In scope:
- automation client startup flow updates
- valueservice client startup flow updates
- zwave client startup flow updates for FileStore-dependent startup path
- shared status-topic conventions under `$MONITOR/<service-short-name>/status`
- configurable FileStore startup retry policy in `[filestore]` INI section
- deployment template updates in `cmake/ini/*.ini`
- deployment documentation updates for new INI keys and lifecycle behavior
- focused tests for startup state machine, retry behavior, and shutdown signaling

Out of scope:
- FileStore service implementation changes
- generic MQTT broker reconnect policy redesign
- unrelated YAHA services without FileStore startup dependency

## Referenced specifications

- [SPEC-automation.md](./SPEC-automation.md)
- [SPEC-valueservice.md](./SPEC-valueservice.md)
- [SPEC-zwave.md](./SPEC-zwave.md)
- [SPEC-filestore.md](./SPEC-filestore.md)
- [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md)

## Canonical status topic contract

For each service, publish lifecycle state to:
- `$MONITOR/automation/status`
- `$MONITOR/valueservice/status`
- `$MONITOR/zwave/status`

Allowed payload values:
- `starting`
- `running`
- `stopped`
- `terminated` (Last Will payload only)

QoS and retain policy:
- QoS: 1
- retain: true

Rationale:
- retained status gives immediate last-known state to observers after subscribe
- Will payload `terminated` marks ungraceful disconnects clearly

## FileStore startup retry configuration

Add two new configurable keys to each service `[filestore]` section:
- `startupRetryCount` (default `10`, range `0..1000`)
- `startupRetryIntervalSeconds` (default `60`, range `1..3600`)

Behavior:
- first FileStore load attempt happens immediately after `starting`
- on failure, retry until attempts are exhausted
- wait exactly `startupRetryIntervalSeconds` between failed attempts
- if all attempts fail, terminate process with non-zero exit status

Interpretation of `startupRetryCount`:
- value `10` means up to 10 retry attempts after first failed attempt
- total max attempts = `1 + startupRetryCount`

## Required runtime sequence per service

Normative startup sequence:
1. create runtime and connect to MQTT broker
2. configure Last Will status topic payload `terminated` for service status topic
3. publish retained `starting` to service status topic
4. execute FileStore startup load path with retry policy
5. if load succeeds: publish retained `running`, then enter normal runtime loop
6. if load fails after retry budget: publish retained `stopped` if connection still alive, then exit process non-zero

Normative shutdown sequence:
1. on graceful stop path (self-termination or received signal): publish retained `stopped`
2. disconnect/close runtime
3. process exits

Ungraceful disconnect path:
- broker publishes retained Will payload `terminated`

## Service-specific implementation notes

## Automation

- Apply sequence to FileStore rules load path (`[filestore].path`).
- Do not enter periodic rule execution loop until `running` state is published.
- If FileStore load fails across retries, terminate before scheduling rule timers.

## ValueService

- Apply sequence to values map load path (`[filestore].filename`).
- Do not accept normal `/set` processing until `running` state is published.
- Preserve existing behavior that successful runtime updates still post full snapshot to FileStore.

## ZWave

- Apply sequence to FileStore settings sync path when `[filestore].use=true`.
- Only start normal ZWave runtime operations after startup FileStore merge succeeds and `running` is published.
- When `[filestore].use=false`, publish `running` directly after `starting` because no FileStore startup dependency exists.

## Implementation phases

## Phase 1: Shared lifecycle policy and contracts

Step 1. Define lifecycle status contract in client runtime specs
- add status topic naming, payload set, qos/retain, and Will semantics
- record strict ordering constraints: connect -> starting -> filestore load -> running

Step 2. Define startup retry policy contract
- add retry count and interval keys under `[filestore]`
- define default values and validation ranges
- define process exit behavior after retry exhaustion

Deliverable:
- stable lifecycle policy that all three clients implement identically

## Phase 2: Configuration mapping updates

Step 3. Extend INI mapping for automation, valueservice, zwave
- parse `filestore.startupRetryCount`
- parse `filestore.startupRetryIntervalSeconds`
- validate ranges with deterministic error messages
- preserve existing defaults when keys are omitted

Step 4. Extend runtime config structs
- add retry fields in each client runtime config object
- keep naming consistent across all three services

Deliverable:
- runtime config exposes retry policy for startup load paths

## Phase 3: Startup state machine implementation

Step 5. Implement status publisher helper per client runtime
- compute service status topic from canonical short name
- publish retained status values with qos 1

Step 6. Implement Last Will setup
- set Will topic/payload to `<status-topic> = terminated`
- ensure Will configuration is active before normal runtime loop

Step 7. Implement startup sequence orchestration
- after broker connect: publish `starting`
- run FileStore load + retry loop
- on success: publish `running`
- on exhausted retries: publish `stopped` if possible, then terminate non-zero

Deliverable:
- deterministic startup lifecycle and failure behavior in all three services

## Phase 4: Shutdown and signal handling

Step 8. Ensure graceful stop always publishes `stopped`
- integrate into existing stop paths and signal handlers
- avoid duplicate status spam (idempotent stop guard)

Step 9. Validate ungraceful termination path
- verify that abrupt disconnect/crash yields broker-published retained `terminated`

Deliverable:
- complete status coverage for graceful and ungraceful exits

## Phase 5: cmake delivery templates and docs

Step 10. Update INI templates in `cmake/ini/`
- `automation.ini`: add and document `startupRetryCount`, `startupRetryIntervalSeconds` in `[filestore]`
- `valueservice.ini`: add and document same keys in `[filestore]`
- `zwave.ini`: add and document same keys in `[filestore]`

Step 11. Update deployment documentation
- update README deployment/config sections to describe:
  - startup ordering and status topic semantics
  - new `[filestore]` retry keys and defaults
  - `terminated` as Will-based ungraceful status
  - `stopped` on graceful self/signal shutdown

Step 12. Verify packaging propagation
- ensure `cmake/create_yaha_deployment.py` copies updated templates unchanged into deployment artifact
- ensure remote deployment flow keeps existing INI overwrite-protection behavior

Deliverable:
- rollout-ready templates and documentation in cmake/deployment path

## Phase 6: Verification and tests

Step 13. Unit tests for config mapping
- parse valid retry values
- reject invalid ranges/types
- verify defaults when omitted

Step 14. Lifecycle tests per service
- successful path: `starting` then `running`
- retry path: failed attempts with configured interval and eventual success
- exhaustion path: `starting`, retries exhausted, `stopped`, process exit failure

Step 15. Shutdown tests
- graceful close publishes `stopped`
- signal-triggered stop publishes `stopped`
- ungraceful disconnect yields retained `terminated` through Will

Deliverable:
- behavior proven for all required lifecycle transitions

## Acceptance criteria

1. Automation, ValueService, and ZWave publish `starting` after MQTT connect and before FileStore load.
2. `running` is published only after required FileStore startup data was loaded successfully.
3. FileStore startup retry behavior is controlled by `[filestore].startupRetryCount` and `[filestore].startupRetryIntervalSeconds`.
4. After retry budget exhaustion, service terminates with non-zero exit status and does not continue normal runtime.
5. On graceful self-stop and signal stop, service publishes `stopped` before disconnect.
6. Unexpected disconnect/crash results in retained `terminated` status via MQTT Will.
7. `cmake/ini/automation.ini`, `cmake/ini/valueservice.ini`, and `cmake/ini/zwave.ini` include the new retry keys with documentation.
8. README deployment/config docs describe the new lifecycle state model and retry settings.

## Risks and mitigations

1. Duplicate status publishes during shutdown
- mitigate with one-shot shutdown state guard.

2. Retry loop blocks signal handling
- mitigate by using interruptible wait and checking shutdown flag between attempts.

3. Inconsistent service short-name mapping
- mitigate by centralizing constants and asserting expected topic names in tests.

4. Will not retained due to broker/client option drift
- mitigate with explicit integration test verifying retained `terminated` observable after abrupt disconnect.

## Step execution order summary

Step 1 -> Step 2 -> Step 3 -> Step 4 -> Step 5 -> Step 6 -> Step 7 -> Step 8 -> Step 9 -> Step 10 -> Step 11 -> Step 12 -> Step 13 -> Step 14 -> Step 15