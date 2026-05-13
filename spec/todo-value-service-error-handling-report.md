# TODO Report: ValueService Error Handling

## Scope

This report analyzes ValueService error handling end-to-end, including:
- value_service retained publish behavior on startup, set, and reload paths
- callback failure handling and resend behavior
- monitor reload and FileStore persistence/read failure visibility
- subscription projection consistency from current key map
- logging correctness for inbound/outbound traces

## Analyzed Sources

- src/yaha_valueserviceclient_main.cpp
- src/yaha/value_service/value_service_component.h
- src/yaha/value_service/value_service_component.cpp
- src/yaha/value_service/SPEC.md
- src/yaha/value_service/test/value_service_component_test.cpp
- src/yaha/value_service_client/value_service_client_app.h
- src/yaha/value_service_client/value_service_client_app.cpp
- src/yaha/value_service_client/SPEC.md
- src/yaha/value_service/test/value_service_client_config_test.cpp

## Key Findings

### P0 Critical

1. Outbound logging can happen before successful send confirmation.
- Evidence:
  - `logMessage("out", message)` runs before callback invocation in retained publish path (src/yaha/value_service/value_service_component.cpp).
- Impact:
  - `value_service[out]` can report success for failed sends.

2. No resend/retry strategy for retained publishes on transient callback failure.
- Evidence:
  - Publish callback throw/failure path has no retry queue in component.
- Impact:
  - Startup replay and runtime retained updates can be lost during transient failures.

### P1 High

3. Publish callback exceptions are silently swallowed.
- Evidence:
  - Empty catch-all block in `publishRetainedValue`.
- Impact:
  - Failure reason/category not observable.

4. FileStore GET/POST failure branches are mostly silent.
- Evidence:
  - `loadValuesFromFileStore` and `persistValuesToFileStore` return bool without structured error logging.
- Impact:
  - Operational diagnosis and automated monitoring are harder.

### P2 Nice To Have

5. Counters for publish attempts/success/failure/retry-exhaustion are missing.

## TODO Backlog

## P0 Must Fix

- [x] Introduce delivery-result-aware retained publish handling.
  - Use callback result category/reason and exception mapping.
  - Acceptance: send is successful only after callback success.

- [x] Fix outbound logging ordering.
  - Emit `value_service[out]` only after confirmed callback send.
  - Emit `value_service[out-fail]` on failure with category and reason.
  - Acceptance: no success log for failed sends.

- [x] Implement bounded retry queue for retained publishes.
  - Retry transient failures on later activity.
  - Drop with explicit `retry_exhausted` log when budget reached.
  - Acceptance: callback recovery flushes queued publishes.

## P1 Should Fix

- [x] Remove silent catch in retained publish path.
  - Emit structured exception/unknown logs.
  - Acceptance: each non-success branch logs exactly once.

- [x] Add structured logs for FileStore GET/POST and reload failures.
  - Include operation, status, and reason.
  - Acceptance: no silent bool-failure path remains in load/persist/reload flow.

## P2 Nice To Have

- [ ] Add optional counters for publish attempt/success/failure/retry exhausted.

## Required Test Extensions

- [x] ValueService component tests:
  - Callback throw path: no false success outbound log.
  - Callback explicit failure result path logs structured category.
  - Retry queue flush after callback restore.
  - Retry queue exhaustion emits `retry_exhausted`.

- [x] ValueService client config tests:
  - Only needed if new config keys are added.

## Summary

ValueService key-map and subscription fundamentals are present, but publish-path robustness and observability are incomplete due to optimistic success logging, missing retry semantics, and silent failure branches. P0/P1 items above are required for robust send/resend and truthful logging behavior.
