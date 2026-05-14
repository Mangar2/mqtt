# TODO Report: HTTP MQTT Interface Error Handling

## Scope

This report analyzes HTTP MQTT interface robustness across:
- request dispatcher and operation contracts
- standalone http_mqtt_interface_client runtime handlers
- broker-forward failure handling and reconnect behavior
- logging correctness for request and broker-forward outcomes

## Analyzed Sources

- src/yaha/http_mqtt_interface/http_mqtt_interface_contracts.cpp
- src/yaha/http_mqtt_interface/http_mqtt_interface_dispatcher.cpp
- src/yaha/http_mqtt_interface/http_mqtt_interface_operations.cpp
- src/yaha/http_mqtt_interface/SPEC.md
- src/yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.cpp
- src/yaha/http_mqtt_interface_client/SPEC.md
- src/yaha/http_mqtt_interface_client/test/http_mqtt_interface_client_app_test.cpp

## Key Findings

### P0 Critical

1. PUT `/publish` and PUT `/pubrel` handlers do not map exceptions to deterministic JSON 500 response.
- Evidence:
  - PUT handlers call interface methods directly without try/catch in client runtime.
- Impact:
  - Error response shape can diverge from POST path and may be framework-dependent.

2. Request-level error logs are missing for PUT failure paths.
- Evidence:
  - POST handlers log `publish_request_failed`; PUT handlers do not.
- Impact:
  - Missing observability for failed native publish/pubrel requests.

### P1 High

3. Broker transport disconnect during shutdown is not guarded.
- Evidence:
  - `brokerTransport.disconnect()` is called without catch in shutdown section.
- Impact:
  - Runtime can throw on shutdown and skip clean exit code path.

### P2 Nice To Have

4. Optional per-endpoint failure counters are missing.

## TODO Backlog

## P0 Must Fix

- [x] Add deterministic exception mapping for PUT publish/pubrel handlers.
  - On exception return JSON 500 compatibility-style internal error response.
  - Acceptance: PUT failure path always returns status 500 with JSON error payload.

- [x] Add request-level structured error logging for PUT publish/pubrel failures.
  - Acceptance: failed PUT request emits `publish_request_failed` line with endpoint and reason.

## P1 Should Fix

- [x] Guard broker disconnect in shutdown path.
  - Log disconnect failure and keep process exit stable.
  - Acceptance: disconnect throw does not crash or change normal exit flow.

## P2 Nice To Have

- [ ] Add optional endpoint failure counters.

## Required Test Extensions

- [x] http_mqtt_interface_client tests:
  - PUT publish failure returns 500 and logs request failure.
  - PUT pubrel failure returns 500 and logs request failure.
  - Shutdown disconnect throw is tolerated.

## Summary

HTTP MQTT interface request and broker-forward behavior is strong on POST compatibility path, but native PUT error handling and shutdown disconnect robustness need explicit hardening for deterministic behavior and complete observability.

## Delta Update 2026-05-14

### Analysis before implementation

New gap identified in broker interplay long run behavior.
Existing tests prove single recovery path only.
Missing deterministic repeated failure and recovery cycle coverage for POST `/publish` broker forward path.

### New TODO backlog delta

## P1 Should Fix

- [x] Add deterministic repeated publish failure and recovery cycle test.
  - Scenario: broker transport publish alternates failure success over multiple requests.
  - Acceptance: responses alternate 500 and 204, reconnect attempts continue, server stays responsive.
  - Test: `run_http_mqtt_interface_client_recovers_across_repeated_broker_publish_failures`.

### Delta summary

Analysis completed first.
Todo updated first.
Implementation status verified after todo update.
Required test exists in module test scope and is validated by client coverage run.

## Delta Update 2026-05-14 Refactoring Skill Architecture Pass

### Analysis before implementation

Current http mqtt interface client violates refactoring architecture law.
Violations:
- fachclient runtime owns broker session state with local `brokerConnected` flag
- fachclient runtime uses direct broker transport callback bundle
- fachclient runtime owns connect publish disconnect paths
- fachclient API exposes callback bypass `publishToBroker`
- main does not compose generic mqtt client and generic runtime orchestrator
- app runtime installs signal handlers instead of generic runtime owning signal policy

### New TODO backlog delta

## P0 Must Fix

- [x] Remove direct broker transport ownership from fachclient runtime.
  - remove local broker session state and transport callbacks from http mqtt interface app API and implementation
  - acceptance: no `YahaMqttClient::Transport` argument and no local `brokerConnected` state in fachclient

- [x] Remove callback bypass communication path.
  - remove `HttpMqttInterfacePublishToBroker` API from fachclient public interface
  - acceptance: broker communication only through IMqttComponent publish callback contract set by generic client

- [x] Introduce domain component boundary.
  - create domain component implementing IMqttComponent for http mqtt interface behavior
  - acceptance: component implements getSubscriptions handleMessage run close setPublishCallback

- [x] Move signal policy to generic runtime.
  - remove signal handler installation from fachclient runtime
  - acceptance: no signal handler setup in http mqtt interface app runtime code

- [x] Main composition must use generic client and runtime.
  - main creates component then mqtt client then generic runtime and runs once
  - acceptance: main uses IMqttComponent coupling and calls generic runtime run entry

## P1 Should Fix

- [x] Align app error contract with yaha-client-architektur mandatory rule.
  - use YahaError for app error returns and throw paths in http mqtt interface client main and app-facing failures
  - acceptance: output text uses YahaError buildMessage for app errors

### Required test delta

- [x] Replace tests that validate forbidden transport bypass API.
  - remove dependency on direct transport injection into fachclient runtime
  - add tests for IMqttComponent boundary behavior and main/generic runtime composition contract where feasible

### Delta summary

Step 1 analysis done.
Step 2 todo report updated before implementation.
Implementation completed for this architecture pass.
HTTP MQTT fachclient now exposes only IMqttComponent domain boundary.
Main composes component plus generic mqtt client plus generic runtime and calls one run entry.
No direct broker transport ownership and no callback bypass API remain in fachclient.
