# TODO Report: ZWave Service Error Handling

## Scope

This report analyzes ZWave service robustness across:
- ZWave service publish callback failure handling
- Controller operation exception handling in message routing
- Startup/shutdown controller failure observability

## Analyzed Sources

- src/yaha/zwave/zwave_service_component.cpp
- src/yaha/zwave/SPEC.md
- src/yaha/zwave/test/zwave_service_component_test.cpp
- src/yaha/zwave_client/SPEC.md

## Key Findings

### P0 Critical

1. Publish callback failures are currently silent in ZWave service.
- Evidence:
  - publish callback return value and exceptions are ignored.
- Impact:
  - Lost outbound telemetry and command acknowledgements are invisible.

2. Controller exceptions in removeFailed/addNode/setValue/requestConfig/close are unguarded.
- Evidence:
  - only scan branch has explicit exception handling.
- Impact:
  - one controller failure can terminate service flow without deterministic error signal.

### P1 High

3. Non-scan controller failures do not emit structured monitoring errors.
- Evidence:
  - no unified error publish/log path for those branches.
- Impact:
  - operators cannot diagnose failing command path quickly.

### P2 Nice To Have

4. Add counters for controller and publish failures.

## TODO Backlog

## P0 Must Fix

- [x] Add structured publish failure logging for callback missing/rejected/exception.
  - Acceptance: deterministic `zwave_service[error] op=publish ...` logs for each non-success category.

- [x] Guard controller exception paths in handleMessage/run/close with deterministic observability.
  - Acceptance: no unhandled exception escapes from these service entrypoints.

## P1 Should Fix

- [x] Publish deterministic monitoring error messages for non-scan controller operation failures.
  - Acceptance: failing removefailed/addnode/setValue/requestConfig/close paths emit `$MONITOR/zwave/error` messages with reason context.

## P2 Nice To Have

- [ ] Add optional failure counters.

## Required Test Extensions

- [x] zwave_service_component tests:
  - publish callback missing/rejected/exception produce deterministic logs
  - removefailed/addnode/setValue exceptions are converted to error monitoring messages
  - run and close exceptions are contained and observable

## Summary

Mandatory non-P2 robustness gaps are implemented and verified: publish failure observability is explicit, controller failure branches are contained with deterministic monitoring errors, and tests/coverage prove the behavior.
