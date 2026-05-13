# TODO Report: Automation Client Error Handling

## Scope

This report analyzes automation-client error handling end-to-end, including:
- automation component behavior
- MQTT runtime reconnect and publish flow
- broker transport ACK/error behavior
- subscription synchronization and rule-change updates
- logging correctness for inbound/outbound traces

## Analyzed Sources

- src/yaha_automationclient_main.cpp
- src/yaha/automation_client/automation_client_component.h
- src/yaha/automation_client/automation_client_component.cpp
- src/yaha/automation_client/automation_client_app.cpp
- src/yaha/mqtt_component/mqtt_component.h
- src/yaha/mqtt_client/mqtt_client.h
- src/yaha/mqtt_client/mqtt_client.cpp
- src/yaha/mqtt_client/broker_transport.cpp
- src/yaha/mqtt_client/mqtt_client_runtime.cpp
- src/yaha/automation_client/test/automation_client_component_test.cpp
- src/yaha/mqtt_client/test/mqtt_client_test.cpp

## Key Findings

### P0 Critical

1. Outbound logging can happen before actual successful send in automation component.
- Evidence:
  - `logOutgoingMessageIfEnabled(outputMessage)` is executed before callback publish in rule output path (src/yaha/automation_client/automation_client_component.cpp:344-345).
  - `logOutgoingMessageIfEnabled(ackMessage)` is executed before callback publish in management ACK path (src/yaha/automation_client/automation_client_component.cpp:534-535).
- Impact:
  - Logs may claim outbound publish even when callback/transport publish fails.
  - Violates trace correctness requirement.

2. No resend/retry for automation-generated outbound messages on publish errors.
- Evidence:
  - `PublishCallback` returns `void` and has no delivery result contract (src/yaha/mqtt_component/mqtt_component.h:22).
  - Outbound publish exceptions in automation are swallowed (`catch (...)`) in both rule output and ACK path (src/yaha/automation_client/automation_client_component.cpp:346-347, 536-537).
  - MQTT transport can throw on write/ACK timeout (e.g. `waitForPubackLocked`, `waitForQos2AckFlowLocked`) (src/yaha/mqtt_client/broker_transport.cpp:700-759, 501-531).
- Impact:
  - Message loss is possible during transient disconnects despite reconnect.
  - Reconnect recovers connection but does not recover already failed automation publishes.

3. Rule update/delete persistence is not transactional; in-memory state can diverge from FileStore on persistence failure.
- Evidence:
  - Delete modifies memory and subscriptions first, then persists; on failure no rollback (src/yaha/automation_client/automation_client_component.cpp:244-251).
  - Update modifies memory and subscriptions first, then persists; on failure no rollback (src/yaha/automation_client/automation_client_component.cpp:262-278).
- Impact:
  - Runtime rules/subscriptions may differ from persisted rules.
  - After restart, behavior can change unexpectedly.

### P1 High

4. Error feedback for management operations is inconsistent and partly misleading.
- Evidence:
  - Update persistence failure returns ACK payload `invalid rule` although rule structure may be valid and error is storage/transport related (src/yaha/automation_client/automation_client_component.cpp:277-279).
  - Delete persistence failure returns no ACK at all (src/yaha/automation_client/automation_client_component.cpp:250-253).
- Impact:
  - Clients cannot reliably distinguish validation errors from persistence failures.
  - Harder automated recovery and operator diagnosis.

5. Multiple failure paths are silent (no structured error log).
- Evidence:
  - FileStore load/persist return bool and callers ignore/flatten failure (src/yaha/automation_client/automation_client_component.cpp:175-207, 226).
  - Internal variable calculation errors swallowed (src/yaha/automation_client/automation_client_component.cpp:324-325).
  - Worker loop catches all exceptions and only transitions to reconnect without reason context (src/yaha/mqtt_client/mqtt_client.cpp:190-191).
- Impact:
  - Root-cause analysis in production is difficult.
  - Monitoring cannot classify failure types.

6. Subscription sync state is optimistic even if subscribe/unsubscribe operations fail.
- Evidence:
  - Runtime sets `activeSubscriptions_ = desiredSubscriptions` unconditionally (src/yaha/mqtt_client/mqtt_client.cpp:293-295).
  - Transport subscribe/unsubscribe can return early on disconnected/no write/timeout with no status returned to caller (src/yaha/mqtt_client/broker_transport.cpp:543-596, 601-651).
- Impact:
  - Runtime may temporarily believe subscriptions are active although broker did not confirm them.
  - Can hide short-lived desynchronization in diagnostics.

### P2 Medium

7. Reconnect diagnostics lack error category details.
- Evidence:
  - Lifecycle logs include reconnect events, but no reason/error class emitted from catch paths (src/yaha/mqtt_client/mqtt_client.cpp:203-207, 190-191).
- Impact:
  - Operators see reconnect churn without direct reason attribution.

8. Tests do not cover key failure semantics required for robust operation.
- Evidence:
  - No unit tests for FileStore persist/load failures in automation component.
  - No tests validating "no outbound log before successful send" semantics.
  - No tests for publish callback throw + retry/resend behavior.
  - No tests for subscription resync correctness when SUBACK/UNSUBACK path fails.

## TODO Backlog

## P0 Must Fix

- [x] Introduce delivery-result aware publish contract.
  - Change component publish callback to return explicit success/failure (or throw typed error) and propagate cause.
  - Ensure automation component can distinguish delivery success from failure.
  - Acceptance: outbound message is considered "sent" only after confirmed callback success.

- [x] Fix outbound logging ordering for automation messages.
  - Move outbound log emission after successful callback return.
  - Add explicit failure log with topic, qos, and error class when callback fails.
  - Acceptance: no `automation_client[out]` line appears for failed sends.

- [x] Implement resend strategy for automation outbound messages on transient publish errors.
  - Add bounded retry queue with backoff and dedup policy per message key/topic+payload+timestamp.
  - Retry on transport-disconnect and ACK-timeout class errors.
  - Acceptance: failed outbound automation message is retried after reconnect until retry budget exhausted.

- [x] Make rule management persistence transactional.
  - Stage update/delete, persist first (or rollback on failure), then commit in-memory state and dynamic subscriptions.
  - Acceptance: memory state and FileStore state remain consistent after any failure.

## P1 Should Fix

- [x] Introduce explicit management ACK error model.
  - Replace generic `invalid rule` for persistence/transport failures with dedicated error payloads (e.g. `persist_failed`, `publish_failed`, `validation_failed`).
  - Always ACK delete/update requests with clear status.
  - Acceptance: callers can classify failures deterministically.

- [x] Add structured error logging for all catch-all and bool-failure paths.
  - Emit operation, topic/keyPath, failure category, and reason text.
  - Minimum paths: FileStore GET/POST, internal variable computation, publish callback, reconnect exception path.
  - Acceptance: every non-success branch emits one machine-parseable error log line.

- [x] Harden subscription synchronization semantics.
  - Track pending SUBSCRIBE/UNSUBSCRIBE confirmation and update active map only after broker confirmation.
  - Add reconcile pass after reconnect and after management/rule reload.
  - Acceptance: runtime active-subscription view matches broker-confirmed state.

## P2 Nice To Have

- [x] Enrich reconnect logs with failure context.
  - Include short reason tags (`connect_failed`, `poll_exception`, `ack_timeout`, `write_failed`).

- [ ] Add metrics counters.
  - `automation_publish_attempt_total`, `automation_publish_success_total`, `automation_publish_failed_total`, `automation_resend_total`, `rules_persist_failed_total`, `subscription_resync_failed_total`.

## Required Test Extensions

- [x] Automation component tests:
  - FileStore GET failure at startup and monitor reload. [done]
  - FileStore POST failure during update and delete. [done]
  - Publish callback throw path: no false outbound success log and retry scheduling. [done]
  - Reconnect + resend queue flush semantics. [done]

- [x] MQTT runtime/transport tests:
  - ACK timeout during QoS1/QoS2 publish triggers retry policy integration. [done]
  - Subscription failure path does not mark subscription as active without confirmation. [done]
  - Reconnect + resend queue flush semantics. [done]

- [x] Logging tests:
  - Assert outbound log only after confirmed send. [done]
  - Assert standardized error log format for each failure class. [done]

## Summary

Current implementation already has good reconnect and subscription replay foundations, but it is not yet reliable for strict error-handling guarantees because delivery confirmation, resend behavior, transactional rule persistence, and logging correctness are incomplete.

The P0 items are required to satisfy: "all error types must be cleanly logged or handled", "MQTT reconnect/send/resend must be robust", "subscriptions must stay current", and "no outbound log before real send".
