# TODO Report: Message Store Service Error Handling

## Scope

This report analyzes message store service robustness across:
- MessageStore lifecycle restore/persist error handling
- MessageStore HTTP runtime startup/listen failure visibility
- MessageStore cleanup command invalid-payload observability
- MessageStore client config parser consistency with documented constraints

## Analyzed Sources

- src/yaha/message_store/message_store.cpp
- src/yaha/message_store/message_store.h
- src/yaha/message_store/SPEC.md
- src/yaha/message_store/test/message_store_test.cpp
- src/yaha/message_store_client/message_store_client_app.cpp
- src/yaha/message_store_client/SPEC.md
- src/yaha/message_store_client/test/message_store_client_app_test.cpp

## Key Findings

### P0 Critical

1. Lifecycle restore/persist failures are silent in MessageStore.
- Evidence:
  - `restoreLatest` and final `persistNow` return values are ignored in run/close paths.
- Impact:
  - Operators cannot detect persistence failures from logs.

2. HTTP listen failure path is not observable in MessageStore server thread.
- Evidence:
  - `httplib::Server::listen(...)` return value is ignored.
- Impact:
  - Service may run without active HTTP endpoint and without diagnostic signal.

### P1 High

3. Cleanup command invalid payload is silently ignored.
- Evidence:
  - cleanup parsing failure branch returns without logging.
- Impact:
  - Invalid operational command usage is invisible.

4. Config parser range for `tree.lengthForFurtherCompression` conflicts with documented behavior.
- Evidence:
  - parser currently enforces minimum `1`, while docs define `0` as valid legacy-preserving value.
- Impact:
  - Valid documented config may be rejected.

### P2 Nice To Have

5. Optional counters for restore/persist/http-listen failures are missing.

## TODO Backlog

## P0 Must Fix

- [x] Add structured lifecycle error logs for restore and final persist failures.
  - Acceptance: failed restore/final persist emits one `message_store[error]` line with operation and reason.

- [x] Add structured HTTP listen failure log.
  - Acceptance: listen false branch logs one deterministic `message_store[error] op=http_listen` line.

## P1 Should Fix

- [x] Log invalid cleanup command payloads.
  - Acceptance: invalid cleanup payload emits one `message_store[error] op=cleanup` line.

- [x] Align parser range with documented behavior for `tree.lengthForFurtherCompression`.
  - Acceptance: config value `0` is accepted and mapped.

## P2 Nice To Have

- [ ] Add optional failure counters for restore/persist/http-listen operations.

## Required Test Extensions

- [x] MessageStore tests:
  - restore failure log for unusable persist directory
  - final persist failure log for unusable persist directory

- [x] MessageStore client tests:
  - `tree.lengthForFurtherCompression=0` accepted

## Summary

MessageStore runtime and parser behavior are largely stable, but mandatory failure observability and one documented parser contract are not fully enforced yet. Non-P2 fixes above close those robustness gaps.
