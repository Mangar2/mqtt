# YAHA Client Message Logging Unification Plan

## Goal

Unify incoming and outgoing message logging across all YAHA clients that already have message-flow logging behavior.
Logging must become a direct capability of shared YAHA message services (`src/yaha/message/*`).
Every emitted message log line must be able to include the full reason chain (`Message.reason()`), not just one flattened reason string.

## Scope

### In scope (modules with existing message-flow logging)

- `src/yaha/mqtt_client`
- `src/yaha/automation_client`
- `src/yaha/zwave`
- `src/yaha/rs485_interface`
- `src/yaha/broker_connector` (source incoming + receiver outgoing paths)
- `src/yaha/http_mqtt_interface_client` (mapped message forward logs)
- `src/yaha/message_store_client` (incoming message logs)

### Not in scope

- Non-message operational logs (startup banners, lifecycle-only logs, HTTP server boot logs).
- New external logger dependencies.
- Full runtime-config migration in one step (compatibility mapping is required first).

## Current Pain Points

- Logging logic is duplicated per client (`std::cout` formatting and reason rendering).
- Reason output is inconsistent (full chain vs partial/plain string).
- Topic-based filtering is not centralized and not consistently available.
- Log schema (fields/order/escaping) is not deterministic across modules.

## Target Architecture

Implement shared logging services under `src/yaha/message/` and make clients call this shared API.

### New shared service units

- `message_log_filter.h/.cpp`
  - Topic wildcard matcher for MQTT-style filters (`+`, `#`).
  - API supports both incoming and outgoing direction checks.
  - Initial implementation supports one filter string and future extension to filter lists/chains.

- `message_log_formatter.h/.cpp`
  - Deterministic log serialization for one `Message`.
  - Must always support rendering full reason array with all entries (`message`, `timestamp`).
  - Reuses existing JSON escaping behavior from message payload services where applicable.

- `message_log_service.h/.cpp`
  - Single entry point for clients.
  - Input: component name, direction (`incoming` or `outgoing`), `Message`, logging config.
  - Output: one deterministic log line string (or skip decision when filtered out).

### Shared logging config model

Define central config value type in message services:

- `enableIncoming`
- `enableOutgoing`
- `includeReasonChain` (default `true`)
- `incomingTopicFilter` (optional)
- `outgoingTopicFilter` (optional)

Compatibility rule:

- Existing client keys keep working.
- Each client maps legacy keys to this shared config model.
- No behavior regression for existing defaults.

### Filter extension point (required now, implementation can be minimal)

The service must expose a centralized filter hook so advanced filters can be added later without per-client changes.

Required now:

- topic wildcard filter support with MQTT semantics (example: `/a/+/+`).
- direction-aware evaluation (`incoming` and `outgoing`).

Prepared for later (not required in this phase):

- central multi-filter chain
- include/exclude precedence rules
- reloadable filter config

## Unified Log Contract

Every message-flow log emitted via shared service must include:

- component identifier
- direction (`incoming` or `outgoing`)
- topic
- value (string/number)
- qos
- retain
- dup
- reason array (all entries, in message order)

Optional fields (only if available):

- raw payload
- transport packet metadata

Reason rule:

- Never truncate reason array.
- Never collapse reason array into one plain reason string in the unified path.

## Migration Phases

## Phase 1: Spec and shared contracts

- Extend message specs to define the unified logging API contract.
- Add test specs for formatter/filter/service modules.
- Define exact field order and escaping rules for deterministic output.

Status:
- Completed on 2026-05-19.

## Phase 2: Shared message logging services

- Implement `message_log_filter.*`, `message_log_formatter.*`, `message_log_service.*`.
- Add focused unit tests:
  - reason chain full rendering
  - special character escaping
  - topic wildcard filtering (`+`, `#`)
  - direction-aware filter behavior
  - disabled logging bypass

Status:
- Completed on 2026-05-19.

## Phase 3: Client adoption wave A

Adopt shared service in:

- `mqtt_client`
- `automation_client`
- `zwave`
- `rs485_interface`

Remove local duplicate message formatting helpers once each client is migrated.

Status:
- Completed on 2026-05-19.

## Phase 4: Client adoption wave B

Adopt shared service in:

- `broker_connector` (source receive + receiver send logging)
- `http_mqtt_interface_client` (broker forward message logs)
- `message_store_client` (incoming logs)

Replace partial/plain reason output with full reason chain output.

## Phase 5: Config unification and compatibility lock

- Introduce shared config mapping helper for message-log settings.
- Keep backward-compatible parsing of existing INI keys.
- Add compatibility tests proving old keys still produce same enable/disable behavior.

## Phase 6: Cleanup and guardrails

- Delete obsolete per-client formatting/filter helpers.
- Add regression tests that compare old expected behavior (where required) with unified output contract.
- Ensure no module bypasses shared service for message-flow logs.

## Acceptance Criteria

- All in-scope clients use shared message logging service for incoming/outgoing message-flow logs.
- Full reason chain output is available in every unified message log path.
- Central topic wildcard filtering is available through shared service API.
- Existing configs remain compatible (no required INI breaking change).
- Duplicate local message-log formatters are removed from migrated modules.
- Client test coverage remains green and threshold-compliant after migration.

## Validation Strategy

- Unit tests in `src/yaha/message/test/` for formatter/filter/service.
- Existing client tests updated only where log format expectations change.
- Full client scope verification via:
  - `python3 test/run_coverage_clients.py`

## Risks and Mitigations

- Risk: subtle log-format regressions break existing operational parsing.
  - Mitigation: deterministic contract tests and staged migration by module.

- Risk: topic filter semantics differ from MQTT expectations.
  - Mitigation: shared matcher tests with explicit wildcard truth table.

- Risk: backward-compat config drift between clients.
  - Mitigation: one shared mapping helper plus per-client compatibility tests.
