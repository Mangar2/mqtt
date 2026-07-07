# TODO Plan: Replace Custom JSON Handling in YAHA Clients with src/json

## Scope

Several YAHA client apps parse or build JSON with their own hand-rolled code
instead of the generic `src/json` module (`JsonValue::parse` /
`JsonValue::stringify`, see `src/json/SPEC.md`). This plan inventories where
that custom JSON handling lives so it can be replaced client by client.

Goal: every client uses `JsonValue` for JSON parsing/serialization. Custom
recursive-descent parsers, manual field-extraction via `find`/`substr`, manual
escaping, and manual string-concatenation JSON builders should be removed.

### Correction from the first version of this plan

The first version of this plan only looked inside `src/yaha/*_client/`
directories and missed most of the actual JSON handling. In this codebase
each client app is really two halves that are wired together in its
`src/yaha_..._main.cpp` entry point:

- `src/yaha/<name>_client/` — CLI/INI config loading, runtime wiring, process
  lifecycle. Usually thin, rarely touches JSON itself.
- `src/yaha/<name>/` (no `_client` suffix) — the actual domain component
  (`XyzComponent`) that talks to the broker or an external HTTP API, and does
  the real JSON parsing/building. **This is where the custom JSON code
  actually lives.**

`message_store_client` is a clear example: `message_store_client_app.cpp`
itself has no JSON code, but the `message_store` library it starts
(`message_store.cpp`, `message_store_json_parser.cpp`) has ~1300 lines of
hand-rolled JSON parsing/serialization. The same client-app/component split
exists for every other client, so the search below was redone by re-checking
each `_client` directory's paired component directory(ies), confirmed via
what each `yaha_..._main.cpp` actually includes/instantiates.

## All 12 YAHA clients, with paired component directory

| # | Main entry point | Client directory | Paired component directory | Own JSON? |
|---|---|---|---|---|
| 1 | `yaha_automationclient_main.cpp` | `automation_client/` | `automation_client/` (own) + `automation/` | Yes — migrated |
| 2 | `yaha_brokerconnectorclient_main.cpp` | `broker_connector_client/` | `broker_connector/` | Yes — migrated |
| 3 | `yaha_filestoreclient_main.cpp` | `file_store_client/` | `file_store/` | Yes — migrated |
| 4 | `yaha_httpmqttinterfaceclient_main.cpp` | `http_mqtt_interface_client/` | `http_mqtt_interface_client/internal/` (own) + `http_mqtt_interface/` | Yes — migrated |
| 5 | `yaha_msgstoreclient_main.cpp` | `message_store_client/` | `message_store/` | Yes — open |
| 6 | `yaha_opensensemapclient_main.cpp` | `opensensemap_client/` | `opensensemap/` | Yes — open |
| 7 | `yaha_pushoverclient_main.cpp` | `pushover_client/` | `pushover/` | Yes — open |
| 8 | `yaha_remoteserviceclient_main.cpp` | `remote_service_client/` | `remote_service/` + `remote_service_http/` | Yes — open (2 duplicate parsers) |
| 9 | `yaha_rs485interfaceclient_main.cpp` | `rs485_interface_client/` | `rs485_interface/`, `rs485_protocol/`, `rs485_state/` | No |
| 10 | `yaha_serialdeviceclient_main.cpp` | `serial_device_client/` | `serial_device/` | Partially — parse side done, build side open |
| 11 | `yaha_valueserviceclient_main.cpp` | `value_service_client/` | `value_service/` | Yes — open |
| 12 | `yaha_zwaveclient_main.cpp` | `zwave_client/` | `zwave_client/` (own, migrated) + `zwave/` | Yes — migrated |

`mqtt_client/` is **not** one of the 12 clients (no `yaha_..._main.cpp` of its
own) — it is shared infrastructure used by all 12 clients, see the "shared
dependency" section below.

## 1. Per-client findings

### 1. automation_client — DONE

- [x] `automation_client/automation_rule_json.cpp` / `.h` — migrated to
  `JsonValue`.
- [x] `automation/rules_tree_json_reader.cpp` / `.h` — migrated to
  `JsonValue`.

### 2. broker_connector_client — DONE

- [x] `broker_connector/relay_component.cpp` — replaced local JSON string
  escaping helper with `JsonValue`-based escaping while preserving raw
  payload rewrite behavior for embedded `message.topic`.
- [x] `broker_connector/source_http_adapter.cpp` — migrated custom
  request/response JSON handling to `JsonValue`:
  - request builders now serialize via `JsonValue::stringify()`
    (`buildConnectPayload`, `buildSubscribePayload`, ping/disconnect payloads)
  - callback error responses now serialize JSON error object via `JsonValue`
  - incoming `/publish` payload parsing now uses `JsonValue::try_parse(...)`
    (topic/value/reason extraction)
  - `/connect` token-object parsing and `/subscribe` qos-array parsing now use
    parsed `JsonValue` object/array access instead of range/token scanning.

### 3. file_store_client — DONE

- [x] `file_store/file_store.cpp` — migrated to `JsonValue`:
  - `validateJsonPayload` now validates by parsing with
    `JsonValue::try_parse(...).has_value()`.
  - `publishMonitoring` now builds payload object and serializes via
    `JsonValue::stringify()`.
  - Text response wrapping on read path now uses
    `JsonValue{body}.stringify()` (also for legacy untyped payload files).

### 4. http_mqtt_interface_client — DONE

- [x] `http_mqtt_interface/http_mqtt_interface_contracts.cpp` and
  `internal/http_mqtt_interface_operations_helpers.cpp` — already use
  `JsonValue` (e.g. `requireJsonObjectPayload` uses
  `JsonValue::try_parse`).
- [x] `internal/http_mqtt_interface_operations_connect_publish.cpp` —
  outgoing request bodies now built via `JsonValue` object serialization
  (connect/publish/disconnect/pubrel).
- [x] `internal/http_mqtt_interface_operations_subscriptions.cpp` —
  subscribe/unsubscribe request payloads now built via `JsonValue`
  serialization.
- [x] `messageValueToJson` / `reasonToJson` in
  `internal/http_mqtt_interface_operations_helpers.cpp` — now implemented
  with `JsonValue`-based serialization.

### 5. message_store_client — DONE

- [x] `message_store/message_store_json_parser.cpp` / `.h` — migrated request
  parsing to `JsonValue::try_parse(...)` with object/array access for both
  `parseSnapshotBody` and `parseSensorPostBody`, while preserving legacy
  sensor defaults and compatibility flags (`history`/`reason`/`time`,
  `levelAmount`/`levelamount`, `nodes`).
- [x] `message_store/message_store.cpp` — migrated HTTP response JSON building
  from manual string concatenation to `JsonValue` object/array construction
  and `.stringify()` (`value`, `reason`, `history`, node arrays, payload wrapper).

### 6. opensensemap_client — DONE

- [x] `opensensemap/opensensemap_component.cpp`:
  - `extractJsonMessage` migrated to `JsonValue::try_parse(...)` with object
    access for the `message` field (fallback to raw payload kept when parse/key/type fails).
  - Request body building migrated to `JsonValue` object serialization for
    `{ "value": <number> }` payload generation.

### 7. pushover_client — DONE

- [x] `pushover/pushover_component.cpp`:
  - migrated response parsing (`status`, `errors`) to
    `JsonValue::try_parse(...)` with object/array/number access.
  - migrated request payload builder to `JsonValue` object serialization
    for token/user/message/priority/title/device fields.

### 8. remote_service_client — DONE

- [x] `remote_service/remote_service_component.cpp` — migrated FileStore
  mapping payload and monitor event payload parsing to `JsonValue::try_parse(...)`
  with object/array access and explicit field/type validation for
  `services/path/devices/qos/reason` and `keyPath`.
- [x] `remote_service_http/remote_service_http_adapter.cpp` — migrated incoming
  HTTP POST payload parsing to `JsonValue::try_parse(...)` with object access for
  `deviceId/state/deviceToken` and `state` conversion for string/number/bool/null.

### 9. rs485_interface_client — NO ACTION NEEDED

- Checked `rs485_interface/`, `rs485_protocol/`, `rs485_state/` — no JSON
  handling anywhere in these directories (protocol is binary/RS485-frame
  based, not JSON).

### 10. serial_device_client — DONE

- [x] `serial_device/serial_device_parser.cpp` — already parses incoming
  frames via `mqtt::json::JsonValue::try_parse` / `JsonValue` accessors. Good
  reference example, same directory as the remaining gap below.
- [x] `serial_device/serial_device_wire_serializer.cpp` — migrated string/value
  escaping and JSON token serialization to `JsonValue` while preserving legacy
  serial field order (`S`,`R`,`C`,`V`) and wire parity.
- `serial_device/serial_device_component.cpp` only calls a shared
  `escapeJsonString` for a log line (~line 610) — no structural JSON there.

### 11. value_service_client — DONE

- [x] `value_service/value_service_component.cpp` — migrated parser and
  serializer to `JsonValue`:
  - `parseValueMapJson` now parses with `JsonValue::try_parse(...)` and
    validates object entries as `string` or integral `number`.
  - `serializeValueMap` now builds a `JsonValue` object and serializes via
    `.stringify()`.
  - `extractJsonStringField` now reads fields from parsed JSON object access
    instead of manual token scanning.

### 12. zwave_client — DONE

- [x] `zwave_client/zwave_client_app.cpp` / `.h` — migrated to `JsonValue`
  (`json/json_error.h`, `json/json_value.h` now included).
- [x] `zwave/zwave_service_component.cpp` — migrated to `JsonValue`:
  - `encodeKnownNodesJson` now builds object/array via `JsonValue` and
    serializes with `.stringify()`.
  - `extractJsonStringField` now parses payload via
    `JsonValue::try_parse(...)` and reads the `keyPath` field from an object.

## 2. Shared dependency used by (almost) every client — flagged separately

- [x] **message/message_payload_codec.cpp** / `.h` (`src/yaha/message/`) — DONE
  - Migrated shared envelope codec parsing/building to `JsonValue`:
    `escapeJsonString`, `serializeReasonArrayOldestFirst`,
    `buildEnvelopePayload`, `parseValueToken`, `parseReasonArray`,
    `parseEnvelopePayload`, and `validateEnvelopeShape` now use
    `JsonValue::try_parse(...)`, typed object/array access, and
    `JsonValue::stringify()` instead of manual range/token scanning.
  - Backward-compatible token behavior is preserved for scalar values:
    string/number remain native `Value` variants; `true`/`false`/`null`
    are still mapped to string values.
  - Reason wire ordering remains oldest-first on serialization and
    parser semantics remain compatible with existing message reason chain use.
- [ ] **message/message_log_formatter.cpp** — calls a shared
  `escapeJsonString` (line 12) to quote a value for a log line; small,
  worth folding into the same cleanup as `message_payload_codec`.

