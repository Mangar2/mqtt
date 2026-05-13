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
