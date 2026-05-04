# Implementation Plan: HTTP MQTT Interface 1.0

This plan defines the implementation sequence for [SPEC-http-mqtt-interface.md](./SPEC-http-mqtt-interface.md).
The scope is the HTTP request/response contract for MQTT operations in interface version 1.0, including browser compatibility for legacy `publish.php` callers.

## Goal

Implement one coherent HTTP MQTT 1.0 interface layer that:

- provides request builders and response validators (`resultCheck`) for all covered MQTT operations
- provides `onX` response builders for server-side responses
- enforces version dispatch behavior
- supports direct browser publish compatibility (`POST /publish`, optional `POST /publish.php`) without requiring PHP bridge scripts

## Scope

In scope:

- shared header and result contract elements
- connect, disconnect, publish, pubrel, subscribe, unsubscribe
- dispatcher version selection behavior
- publish compatibility profile (`/publish` and `/publish.php` alias behavior)
- error handling and backward compatibility behavior defined in spec

Out of scope in this plan:

- MQTT wire protocol implementation
- broker-side session persistence behavior
- non-1.0 versions
- UI or dashboard concerns

## Referenced specifications

- [SPEC-http-mqtt-interface.md](./SPEC-http-mqtt-interface.md)
- [SPEC-message.md](./SPEC-message.md)
- [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md)

## Target module structure

Create or align module boundaries so responsibilities stay clear.

1. Shared contract module
- `IResult`, `RequestDataV2`, shared header defaults
- shared parsing and guard helpers

2. Operation modules (v1.0)
- `connect`
- `disconnect`
- `publish`
- `pubrel`
- `subscribe`
- `unsubscribe`

Each operation module provides:
- request builder
- `resultCheck`
- `onX` response builder

3. Version dispatcher module
- `getVersion` behavior
- top-level `Interfaces` API map and dispatchers

4. Compatibility adapter for browser publish
- maps compatibility POST input to Publish 1.0 payload and headers
- enforces compatibility response mode and error mapping

## Implementation phases

## Phase 1: Shared contracts and invariants

Step 1. Implement shared type and header constants
- implement `IResult` and `RequestDataV2` shape used by all operations
- implement standard JSON and text header defaults
- ensure all builders use consistent defaults
- Reuse info:
  - `broker_connector/source_http_adapter.cpp`: `makeStandardJsonHeaders()` can be moved almost unchanged into a shared HTTP MQTT 1.0 header helper.
  - `broker_connector/source_http_adapter.cpp`: current callback responses already use `content-type` and `version=1.0` consistently and can serve as baseline for `onX` header defaults.

Step 2. Implement shared validation helpers
- content-type prefix checks
- required header extraction (`packet`, `packetid`, `version`)
- JSON parse helper with deterministic error messages
- Reuse info:
  - `broker_connector/source_http_adapter.cpp`: `responseHeader()`, `parseUnsigned16()`, and `parseBool()` can be reused with little change as generic low-level header/value helpers.
  - `broker_connector/source_http_adapter.cpp`: string-token JSON extract helpers (`tryFindObjectRange`, `tryExtractKeyStringValue`, `tryExtractKeyValueToken`) can be reused as temporary parser utilities where strict schema JSON parser is not yet introduced.

Deliverable:
- all operations can reuse one shared contract layer with no duplicated low-level checks

## Phase 2: Version dispatch foundation

Step 3. Implement `getVersion` behavior
- if `headers.version` is not a string, default to version `0.0`
- dispatch map lookup by selected version
- if version not implemented in publish map, throw `undefined version <version>`
- Reuse info:
  - No direct ready-made dispatcher in `broker_connector` or `broker_connector_client`; implement new map-based dispatcher for this step.

Step 4. Implement top-level `Interfaces` export shell
- define method signatures for all required API entries
- wire methods through version maps
- keep non-implemented versions explicitly absent
- Reuse info:
  - No direct `Interfaces` facade in `broker_connector` modules; only function-level protocol calls exist there.

Deliverable:
- stable API surface and deterministic version selection behavior

## Phase 3: Core operation pairs (connect/disconnect)

Step 5. Implement Connect 1.0
- request builder: force `keepAlive=0` when undefined
- include `version=1.0` and standard JSON headers
- `resultCheck`: enforce `200`, `connack`, JSON payload shape, `present`, `mqttcode`, token fields
- `onConnect`: produce exact `connack` response contract
- Reuse info:
  - `broker_connector/source_http_adapter.cpp`: `buildConnectPayload()` and `sendConnect()` already implement most connect request and response checks (status, `packet=connack`, token object with `send`/`receive`).
  - `broker_connector/source_http_adapter_test.cpp`: connect success/failure tests can be adapted with little change for Connect 1.0 contract tests.

Step 6. Implement Disconnect 1.0
- request builder payload `{ clientId }`
- `resultCheck`: enforce `204`
- `onDisconnect`: `204` with empty payload and version header
- Reuse info:
  - `broker_connector/source_http_adapter.cpp`: `sendDisconnect()` already maps directly to `PUT /disconnect` with `204` success check.

Deliverable:
- connect/disconnect request+response contracts fully spec-compliant

## Phase 4: Publish and Pubrel flow

Step 7. Implement Publish 1.0
- request builder defaults: `qos=1`, `retain=false`, `dup=false`
- forward only `topic`, `value`, `reason` from message
- set headers `qos`, `dup`, `retain`, `version`, optional `packetid`
- `resultCheck`: enforce status and packet rules by QoS and packetid echo
- `onPublish`: return `204`, echo qos/retain/packetid, map packet header by qos (`puback`/`pubrec`)
- Reuse info:
  - `broker_connector/source_http_adapter.cpp`: `/publish` callback handler already implements QoS/retain/dup header parsing, packetid echo, and QoS-based ack mapping (`puback`/`pubrec`).
  - `broker_connector/source_http_adapter.cpp`: `parseIncomingMessageBody()` already enforces minimal publish payload shape (`topic` + `value`) and can be refactored into shared publish payload parser.
  - `broker_connector/source_http_adapter_test.cpp`: publish callback tests already cover qos0/qos1/qos2 ack behavior and can be reused as contract tests.

Step 8. Implement Pubrel 1.0
- request builder with text headers + optional `packetid`
- `resultCheck`: `204`, `pubcomp`, packetid matching
- `onPubrel`: return `204`, `packet=pubcomp`, echoed packetid
- Reuse info:
  - `broker_connector/source_http_adapter.cpp`: `/pubrel` handler is nearly identical to required `onPubrel` behavior (status `204`, `packet=pubcomp`, packetid echo).

Deliverable:
- complete QoS-related publish and release handshake contracts

## Phase 5: Subscribe and Unsubscribe flow

Step 9. Implement Subscribe 1.0
- request builder with packetid in header
- `resultCheck`: enforce `200`, `suback`, packetid equality, allowed qos codes
- `onSubscribe`: return `200`, `suback`, JSON payload and packetid echo
- Reuse info:
  - `broker_connector/source_http_adapter.cpp`: `sendSubscribe()` already validates `status=200`, `packet=suback`, packetid echo, and response QoS array semantics.
  - `broker_connector/source_http_adapter.cpp`: `tryParseQosArray()` can be reused with little change as subscribe result parser.

Step 10. Implement Unsubscribe 1.0
- request builder with packetid in header
- `resultCheck`: accept `200` or `204` with backward-compatible empty payload rule
- validate return codes when payload is present (`0x00`, `0x11`)
- `onUnsubscribe`: return `200`, `unsuback`, packetid echo
- Reuse info:
  - No direct unsubscribe implementation in `broker_connector`; reuse subscribe packetid/header validation pattern from `sendSubscribe()` and extend for unsubscribe-specific status/payload rules.

Deliverable:
- subscription lifecycle contract complete with backward compatibility rule

## Phase 6: Browser compatibility profile (`publish.php` replacement)

Step 11. Implement compatibility input parser
- accepted methods/endpoints:
  - `POST /publish`
  - `POST /publish.php` (alias, deployment-switchable)
  - `PUT /publish` native path
- accepted content types:
  - `application/x-www-form-urlencoded`
  - `application/json`
  - `text/plain` containing JSON object
- extraction and normalization:
  - read `topic` and `value` from form/query first
  - fallback to JSON body when topic missing
  - normalize topic by decoding `%2F` -> `/` (case-insensitive)
- Reuse info:
  - `broker_connector/source_http_adapter.cpp`: existing `/publish` body parsing and bad-payload rejection (`400`) provide a reusable skeleton for compatibility parsing.
  - `broker_connector_client/broker_connector_client_app.cpp`: INI option parsing style can be reused for deployment flags (e.g. enable/disable `/publish.php` alias, legacy response mode).

Step 12. Implement compatibility mapping and defaults
- reject empty/missing topic with `400`
- default `qos=1`, `retain=0`
- if reason is missing, generate browser default reason entry with RFC3339/ISO-8601 timestamp
- map to native Publish 1.0 internal call with exact payload fields
- Reuse info:
  - `broker_connector/source_http_adapter.cpp`: defaulting behavior around qos/retain headers and normalized message handoff is already present and can be adapted for compatibility mapping.

Step 13. Implement compatibility response modes
- native mode: `204` empty payload
- legacy php-compatible mode: `200` with JSON payload containing stringified downstream payload
- error mapping: invalid JSON `400`, unsupported method `405`, internal failure `500`
- Reuse info:
  - `broker_connector/source_http_adapter.cpp`: existing response construction for successful publish/pubrel and `400` rejection path can be reused as baseline response helpers.

Deliverable:
- deployments can remove `publish.php` while keeping compatible client behavior

## Phase 7: Verification

Step 14. Unit tests for shared validators and each operation pair
- positive and negative tests for all `resultCheck` rules
- builder defaults and header generation tests
- dispatcher fallback and undefined version exception tests
- Reuse info:
  - `broker_connector/test/source_http_adapter_test.cpp`: `FakeSourceHttpBroker` and endpoint-oriented test style can be reused with low changes for request/response contract unit tests.

Step 15. Integration tests for compatibility profile
- `POST /publish` with form body
- `POST /publish` with JSON body
- `POST /publish.php` alias behavior
- native vs legacy response mode behavior
- malformed/missing input error code checks
- Reuse info:
  - `broker_connector/test/source_http_adapter_test.cpp`: local httplib server setup and callback probing patterns can be reused for compatibility endpoint integration tests.

Step 16. End-to-end contract tests
- full sequence: connect -> subscribe -> publish -> pubrel -> unsubscribe -> disconnect
- packet header and packetid echo assertions at each step
- Reuse info:
  - `broker_connector/test/source_http_adapter_test.cpp`: existing sequence tests already cover connect/subscribe/publish/pubrel and can be extended to include unsubscribe/disconnect contract assertions.

Deliverable:
- verified behavior against all normative rules in spec

## Acceptance criteria

1. All API entries in exported `Interfaces` are implemented for version `1.0`.
2. Shared defaults and response envelopes are consistent across operations.
3. Dispatcher behavior matches spec for missing/non-string/undefined versions.
4. All operation `resultCheck` functions enforce the documented status/header/payload rules.
5. Browser compatibility profile supports both modern and legacy caller modes.
6. Tests cover success paths and expected failure paths for every operation pair.

## Risks and implementation notes

1. Header typing and case handling
- normalize incoming header keys before validation to avoid false negatives.

2. Loose payload typing
- use explicit guards for required fields (`present`, token fields, return code arrays) to prevent silent contract drift.

3. Backward compatibility branch complexity
- keep compatibility mapping in a dedicated module to avoid polluting native Publish 1.0 logic.

## Step execution order summary

Step 1 -> Step 2 -> Step 3 -> Step 4 -> Step 5 -> Step 6 -> Step 7 -> Step 8 -> Step 9 -> Step 10 -> Step 11 -> Step 12 -> Step 13 -> Step 14 -> Step 15 -> Step 16

Contract foundations first, operation pairs second, compatibility profile third, tests last.
