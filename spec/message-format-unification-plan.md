# Message Format Unification Plan (YAHA Clients)

## Goal

Unify message format handling across all YAHA clients so that:
- message field access follows one shared approach,
- message envelope construction follows one shared approach,
- transport payload shape is deterministic and identical across clients,
- no client keeps private JSON parser/builder behavior for YAHA message envelope semantics.

Primary reference behavior:
- [spec/@mangar2/mqtt-utils/src/message.ts](spec/@mangar2/mqtt-utils/src/message.ts)

Primary specification to enforce:
- [spec/yaha/SPEC-message.md](spec/yaha/SPEC-message.md)

## Scope Inventory

### Canonical Transport Path

- [src/yaha/mqtt_client/broker_transport.cpp](src/yaha/mqtt_client/broker_transport.cpp)
  - builds outbound payload envelope
  - parses inbound forwarded envelope
  - currently the operational source of truth for wire shape

### Modules with Own Message/JSON Logic (Candidates for Consolidation)

- [src/yaha/automation_client/automation_trace_format.cpp](src/yaha/automation_client/automation_trace_format.cpp)
  - own JSON escaping and message trace payload build
- [src/yaha/remote_service/remote_service_component.cpp](src/yaha/remote_service/remote_service_component.cpp)
  - custom JSON token parsing helpers
- [src/yaha/remote_service_http/remote_service_http_adapter.cpp](src/yaha/remote_service_http/remote_service_http_adapter.cpp)
  - custom JSON token parsing helpers
- [src/yaha/value_service/value_service_component.cpp](src/yaha/value_service/value_service_component.cpp)
  - own JSON parsing/serialization helpers

### Modules Using Message API but Not Owning Envelope Format

- [src/yaha/zwave_client](src/yaha/zwave_client)
- [src/yaha/message_store_client](src/yaha/message_store_client)
- [src/yaha/file_store_client](src/yaha/file_store_client)
- [src/yaha/broker_connector_client](src/yaha/broker_connector_client)
- [src/yaha/http_mqtt_interface_client](src/yaha/http_mqtt_interface_client)
- [src/yaha/rs485_interface_client](src/yaha/rs485_interface_client)

## Target Architecture

## Shared Interface

Introduce one shared message-format utility surface under `src/yaha/message/`.

### Parsing API

- `parseEnvelopePayload(payloadText, mqttTopic, qos, retain, dup) -> ParseResult`
- `parseReasonArray(jsonText) -> vector<ReasonEntry>`
- `parseValueToken(jsonToken) -> Value`

### Serialization API

- `buildEnvelopePayload(const Message&) -> std::string`
- `serializeReasonArrayOldestFirst(const vector<ReasonEntry>&) -> std::string`
- `escapeJsonString(std::string_view) -> std::string`

### Validation API

- `validateEnvelopeShape(jsonText) -> bool/error`
- `validateReasonEntry(const ReasonEntry&) -> bool/error`

## Contract Rules (Must Hold Everywhere)

- Outbound payload is always YAHA envelope JSON:
  - `{"message":{"topic":...,"value":...,"reason"?:[...]}}`
- `message.topic` and `message.value` are required.
- `message.reason` is optional and omitted when empty.
- Reason order on wire is oldest-first.
- New timestamps are ISO 8601 UTC.
- All JSON strings are escaped with one shared implementation.

## Migration Phases

### Phase 1: Specification Hardening (this phase)

- Make [spec/yaha/SPEC-message.md](spec/yaha/SPEC-message.md) explicit and normative:
  - exact envelope schema
  - required/optional fields
  - reason ordering
  - timestamp format requirements
  - explicit reference to TS original behavior

### Phase 2: Shared Utility Extraction

- Add shared parser/builder helpers in `src/yaha/message/`.
- Add focused unit tests for edge cases (escaping, malformed JSON, reason order, numeric/string coercion).

### Phase 3: Transport Alignment

- Ensure [src/yaha/mqtt_client/broker_transport.cpp](src/yaha/mqtt_client/broker_transport.cpp) uses only shared parser/builder.

### Phase 4: Client Consolidation

- Replace custom JSON message handling in:
  - [src/yaha/automation_client/automation_trace_format.cpp](src/yaha/automation_client/automation_trace_format.cpp)
  - [src/yaha/remote_service/remote_service_component.cpp](src/yaha/remote_service/remote_service_component.cpp)
  - [src/yaha/remote_service_http/remote_service_http_adapter.cpp](src/yaha/remote_service_http/remote_service_http_adapter.cpp)
  - [src/yaha/value_service/value_service_component.cpp](src/yaha/value_service/value_service_component.cpp)

### Phase 5: Cleanup and Guardrails

- Remove duplicate local parser/serializer helpers.
- Add regression tests proving identical behavior against TS reference format.

Status:
- Completed: shared message-format helpers are now reused by mqtt transport and phase-4 client modules.
- Completed: regression tests added in `src/yaha/message/test/message_payload_codec_test.cpp`
  for TS-compatible envelope ordering and parse/rebuild behavior.

## Acceptance Criteria

- One canonical envelope format for all outbound YAHA client messages.
- One shared parser/builder implementation reused across YAHA modules.
- No private per-client envelope parser/builder logic remains.
- Specs and tests are synchronized and explicit.
- Workspace Problems panel shows zero errors/warnings for changed files.
