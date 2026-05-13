# TODO Report: File Service Error Handling

## Scope

This report analyzes file-service error handling end-to-end, including:
- file_store monitoring publish behavior
- file_store callback failure handling and resend behavior
- file_store HTTP persistence/read error visibility
- file_store_client runtime config mapping robustness
- logging correctness for outbound monitoring traces

## Analyzed Sources

- src/yaha_filestoreclient_main.cpp
- src/yaha/file_store/file_store.h
- src/yaha/file_store/file_store.cpp
- src/yaha/file_store/SPEC.md
- src/yaha/file_store/test/file_store_test.cpp
- src/yaha/file_store_client/file_store_client_app.h
- src/yaha/file_store_client/file_store_client_app.cpp
- src/yaha/file_store_client/SPEC.md
- src/yaha/file_store_client/test/file_store_client_app_test.cpp

## Key Findings

### P0 Critical

1. Outbound monitoring logging can happen before actual successful send.
- Evidence:
  - `logMessage("out", monitoringMessage)` is executed before callback invocation in monitoring path ([src/yaha/file_store/file_store.cpp](src/yaha/file_store/file_store.cpp)).
- Impact:
  - Logs may claim outbound publish even when callback publish fails.
  - Violates trace correctness requirement.

2. No resend/retry policy for monitoring messages on transient publish failures.
- Evidence:
  - Monitoring callback result/exception is ignored and no retry queue exists in publish path ([src/yaha/file_store/file_store.cpp](src/yaha/file_store/file_store.cpp)).
- Impact:
  - Monitoring events can be lost during transient disconnects/ACK timeout failures.

### P1 High

3. Publish callback failures are silently swallowed.
- Evidence:
  - Catch-all block in `publishMonitoring` is empty ([src/yaha/file_store/file_store.cpp](src/yaha/file_store/file_store.cpp)).
- Impact:
  - Failure reason/category is not observable.
  - Harder operational diagnosis.

4. Callback-missing state is not observable.
- Evidence:
  - Monitoring publish path returns silently when callback is unset ([src/yaha/file_store/file_store.cpp](src/yaha/file_store/file_store.cpp)).
- Impact:
  - Early runtime monitoring events disappear without explicit trace.

### P2 Nice To Have

5. Structured counters for monitoring send attempts are missing.
- Evidence:
  - No metrics counters for attempt/success/failure/retry-exhausted in FileStore module.

## TODO Backlog

## P0 Must Fix

- [x] Introduce delivery-result-aware monitoring publish handling in FileStore.
  - Use callback result category/reason for success/failure decision.
  - Acceptance: message considered sent only after callback success.

- [x] Fix outbound monitoring logging ordering.
  - Emit `file_store[out]` only after successful callback send.
  - Add explicit `file_store[out-fail]` logs on failure with category and reason.
  - Acceptance: no success outbound log for failed sends.

- [x] Implement bounded retry queue for monitoring events.
  - Retry transient callback failures on later activity cycles.
  - Emit explicit retry-exhausted failure log and drop after budget.
  - Acceptance: transient failure followed by callback recovery eventually sends queued event.

## P1 Should Fix

- [x] Remove silent catch blocks in monitoring publish path.
  - Emit structured error lines for exception and unknown exception branches.
  - Acceptance: every failure branch emits exactly one machine-parseable line.

- [x] Make callback-missing behavior explicit and recoverable.
  - Log callback-missing with structured category and enqueue retryable event.
  - Acceptance: callback-restore flow flushes queued events on next trigger.

## P2 Nice To Have

- [ ] Add optional counters for monitor publish attempt/success/failure/retry exhausted.

## Required Test Extensions

- [x] FileStore tests:
  - Publish callback throw path: no false outbound success log.
  - Publish callback explicit failure result path logs `out-fail`.
  - Retry queue flush after callback recovery.
  - Retry budget exhaustion logs explicit failure category.

- [x] FileStore client tests:
  - Only required if new runtime config keys are added for retry policy.

## Summary

File service persistence and HTTP error mapping are already strong, but monitoring send reliability and observability are incomplete: outbound success logging is optimistic, callback failures can be silent, and resend policy is missing.

P0/P1 items above are required to satisfy robust reconnect/send/resend and truthful logging requirements for FileStore monitoring behavior.
