# TODO Report: Remote Service Error Handling

## Scope

This report analyzes remote service robustness across:
- RemoteServiceComponent publish failure observability
- FileStore mapping reload failure observability
- RemoteService standalone HTTP listen failure observability

## Analyzed Sources

- src/yaha/remote_service/remote_service_component.cpp
- src/yaha/remote_service/SPEC.md
- src/yaha/remote_service/test/remote_service_component_test.cpp
- src/yaha_remoteserviceclient_main.cpp
- src/yaha/remote_service_client/SPEC.md

## Key Findings

### P0 Critical

1. Publish callback non-success branches are not logged.
- Evidence:
  - publishCommand returns PublishFailed but emits no failure context.
- Impact:
  - Operators cannot distinguish callback-missing, callback-throw, transport reject, or reason text.

2. Standalone runtime HTTP listen failure is silent.
- Evidence:
  - httplib listen return value in main thread is ignored.
- Impact:
  - Process can run without active API endpoint and no deterministic failure signal.

### P1 High

3. Publish callback exception branch has no explicit reason classification in logs.
- Evidence:
  - catch-all path returns PublishFailed only.
- Impact:
  - Failure triage loses exception-vs-transport separation.

### P2 Nice To Have

4. Add structured counters for reload/publish failures.

## TODO Backlog

## P0 Must Fix

- [x] Add structured publish failure logs in publishCommand.
  - Acceptance: callback-missing and callback result false emit deterministic remote_service error logs with category/reason.

- [x] Add structured HTTP listen failure log in standalone runtime.
  - Acceptance: listen false branch logs one deterministic remoteservice_client error line with host/port.

## P1 Should Fix

- [x] Log publish callback exceptions with explicit reason.
  - Acceptance: exception path emits deterministic remote_service error log line.

## P2 Nice To Have

- [ ] Add optional counters for publish and reload failures.

## Required Test Extensions

- [x] remote_service_component tests:
  - callback result false logs structured failure details
  - callback exception logs structured failure details
  - callback missing logs structured failure details

## Summary

RemoteService mapping and command behavior are functionally covered, but mandatory failure observability on publish paths and runtime listen path needs hardening. Non-P2 fixes above close the key operational blind spots.
