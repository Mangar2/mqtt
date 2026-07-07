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
| 3 | `yaha_filestoreclient_main.cpp` | `file_store_client/` | `file_store/` | Yes — open |
| 4 | `yaha_httpmqttinterfaceclient_main.cpp` | `http_mqtt_interface_client/` | `http_mqtt_interface_client/internal/` (own) + `http_mqtt_interface/` | Yes — partially open |
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

### 3. file_store_client — OPEN

- [ ] `file_store/file_store.cpp` — local `jsonEscape` (line 640):
  - `validateJsonPayload` (~line 482): shallow "does this look like JSON"
    check (only inspects the first non-whitespace character). FileStore
    stores caller-provided JSON as an opaque blob, so this could become a
    real `JsonValue::try_parse(...).has_value()` check — stricter and
    simpler than the current heuristic.
  - `publishMonitoring` (~line 505): hand-built monitoring-event JSON
    payload (`{"keyPath":...,"directory":...,"changeType":...,"timestamp":...,"source":...,"details":...}`)
    via string concatenation and `std::format` with `jsonEscape` — a
    straightforward `JsonValue::stringify()` replacement.
  - Response wrapping at read time (~lines 467, 476) wraps a stored string in
    quotes via `std::format("\"{}\"", jsonEscape(body))` — same pattern,
    replaceable with `JsonValue{body}.stringify()`.

### 4. http_mqtt_interface_client — PARTIALLY OPEN

- [x] `http_mqtt_interface/http_mqtt_interface_contracts.cpp` and
  `internal/http_mqtt_interface_operations_helpers.cpp` — already use
  `JsonValue` (e.g. `requireJsonObjectPayload` uses
  `JsonValue::try_parse`).
- [ ] `internal/http_mqtt_interface_operations_connect_publish.cpp` — still
  builds outgoing broker request bodies by hand via `std::format` +
  `escapeJsonString`, e.g. connect payload (~line 48-60), publish payload
  (~line 175-178 using `messageValueToJson`/`reasonToJson`), disconnect
  payload (~line 243).
- [ ] `internal/http_mqtt_interface_operations_subscriptions.cpp` — same
  pattern for subscribe/unsubscribe request bodies (~lines 19-107).
- [ ] `messageValueToJson` / `reasonToJson` in
  `internal/http_mqtt_interface_operations_helpers.cpp` (~lines 103-125) —
  hand-built value/reason JSON fragments instead of building a `JsonValue`
  tree and calling `.stringify()`.
- This client is the most-migrated one already; the remaining gap is
  specifically the *outgoing request body* builders, not the response
  parsing (which is already on `JsonValue`).

### 5. message_store_client — OPEN

(unchanged from previous version of this plan)

- [ ] `message_store/message_store_json_parser.cpp` / `.h` (845 / 46 lines):
  fully custom parser for incoming HTTP request bodies — `parseSnapshotBody`,
  `parseSensorPostBody` — no `json/json_value.h` include at all.
- [ ] `message_store/message_store.cpp` (~lines 459-546): hand-rolled
  string-concatenation JSON response builder — `jsonStringLiteral`,
  `valueToJson`, `reasonsToJson`, `historyToJson`, `nodeToJson`,
  `nodesToJson`, `wrapPayloadObject`. Only individual string values are
  escaped via `JsonValue{...}.stringify()`; the surrounding object/array
  structure is built by hand.

### 6. opensensemap_client — OPEN

- [ ] `opensensemap/opensensemap_component.cpp`:
  - `extractJsonMessage` (~line 251): manual `find("\"message\"")` field
    extraction from the openSenseMap API response instead of
    `JsonValue::parse(...)["message"]`.
  - Request body building (~line ~220): `stream << R"({"value":)" <<
    numericValue << '}';` — trivial today, but should still go through
    `JsonValue` for consistency.

### 7. pushover_client — OPEN

- [ ] `pushover/pushover_component.cpp`:
  - `tryExtractJsonInteger` (~line 54) and `tryExtractJsonArray` (~line 103):
    manual key-search field extraction from the Pushover API response.
  - Request payload builder (~line 291): `R"({"token":")" <<
    escapeJsonString(token) << ...` manual concatenation for
    token/user/message/title/device fields.

### 8. remote_service_client — OPEN (largest gap besides value_service)

- [ ] `remote_service/remote_service_component.cpp` — full hand-written
  recursive-descent JSON parser: `parseJsonString`, `parseJsonUnsignedInteger`,
  `skipJsonValue`, `skipJsonObject`, `skipJsonArray`, `skipJsonNumber`
  (~lines 42-665), used to parse FileStore-persisted service mapping config
  and monitor payloads.
- [ ] `remote_service_http/remote_service_http_adapter.cpp` — a **second,
  separate** hand-written JSON parser: `parseJsonString`,
  `parseJsonValueToken`, `parseFlatJsonObject` (~lines 34-285), used to parse
  incoming HTTP POST request bodies.
- These two files duplicate the same kind of parsing logic independently of
  each other — migrating both to `JsonValue` also removes the duplication
  between them.

### 9. rs485_interface_client — NO ACTION NEEDED

- Checked `rs485_interface/`, `rs485_protocol/`, `rs485_state/` — no JSON
  handling anywhere in these directories (protocol is binary/RS485-frame
  based, not JSON).

### 10. serial_device_client — PARTIALLY OPEN

- [x] `serial_device/serial_device_parser.cpp` — already parses incoming
  frames via `mqtt::json::JsonValue::try_parse` / `JsonValue` accessors. Good
  reference example, same directory as the remaining gap below.
- [ ] `serial_device/serial_device_wire_serializer.cpp` — local
  `escapeJsonString` (~line 42) plus hand-built outgoing wire JSON:
  `endpointToJsonText`, `valueToJsonText`, and the final concatenation
  `{"S":...,"R":...,"C":"...","V":...}` (~lines 58-113). Parsing already
  uses `JsonValue`; serialization still doesn't.
- `serial_device/serial_device_component.cpp` only calls a shared
  `escapeJsonString` for a log line (~line 610) — no structural JSON there.

### 11. value_service_client — OPEN (largest gap)

- [ ] `value_service/value_service_component.cpp` — full hand-written parser
  and serializer, closely mirroring what `zwave_client_app.cpp` used to have
  before its migration:
  - Parser: `parseJsonStringToken`, `parseJsonIntegerToken`,
    `parseValueMapEntry`, `parseValueMapJson` (~lines 57-530).
  - Serializer: manual `jsonText` concatenation building a flat
    `{"key":"value"|number, ...}` object (~lines 465-486), plus
    `extractJsonStringField` (~line 417) for reading individual fields from
    FileStore responses.
  - This is the best candidate to migrate next after `message_store_client`,
    given its similarity to the already-completed `zwave_client_app.cpp`
    migration (same author style, same scale).

### 12. zwave_client — DONE

- [x] `zwave_client/zwave_client_app.cpp` / `.h` — migrated to `JsonValue`
  (`json/json_error.h`, `json/json_value.h` now included).
- [x] `zwave/zwave_service_component.cpp` — migrated to `JsonValue`:
  - `encodeKnownNodesJson` now builds object/array via `JsonValue` and
    serializes with `.stringify()`.
  - `extractJsonStringField` now parses payload via
    `JsonValue::try_parse(...)` and reads the `keyPath` field from an object.

## 2. Shared dependency used by (almost) every client — flagged separately

- [ ] **message/message_payload_codec.cpp** / `.h` (`src/yaha/message/`, 609 lines)
  - Not a `_client` directory itself, but implements the full custom JSON
    envelope codec used by `mqtt_client` for every published/subscribed
    message: `escapeJsonString`, `buildEnvelopePayload`, `parseValueToken`,
    `parseReasonArray`, `parseEnvelopePayload`, `validateEnvelopeShape`.
  - Because every `*_client` app sends/receives messages through
    `mqtt_client`, this codec is indirectly used by all 12 clients.
  - Treat as its own migration item, not a "client": it sits on the hot path
    for every MQTT message, so replacing it with `JsonValue` needs a
    performance check (parse/stringify cost per message) before rollout.
- [ ] **message/message_log_formatter.cpp** — calls a shared
  `escapeJsonString` (line 12) to quote a value for a log line; small,
  worth folding into the same cleanup as `message_payload_codec`.

## Suggested order

1. ~~`automation_client` + `automation/rules_tree_json_reader`~~ — done.
2. ~~`zwave_client/zwave_client_app.cpp` + `zwave/zwave_service_component.cpp`~~ — done.
3. `message_store_client` (`message_store_json_parser` +
   `message_store.cpp` response builder) — self-contained, moderate size,
   HTTP request/response path, not per-MQTT-message hot path.
4. `value_service_client` (`value_service/value_service_component.cpp`) —
   self-contained, same scale/shape as the already-migrated
   `zwave_client_app.cpp`, good template reuse.
5. ~~`zwave/zwave_service_component.cpp`~~ — done.
6. `serial_device/serial_device_wire_serializer.cpp` — small, serializer
   only, parser side is already done.
7. `opensensemap/opensensemap_component.cpp`,
   `pushover/pushover_component.cpp` — small, similar shape (HTTP API
   response field extraction + request body build).
8. `file_store/file_store.cpp` — small, monitoring payload + blob
   passthrough validation.
9. ~~`broker_connector/relay_component.cpp` +
  `broker_connector/source_http_adapter.cpp`~~ — done.
10. `remote_service/remote_service_component.cpp` +
    `remote_service_http/remote_service_http_adapter.cpp` — largest
    remaining item besides `message_payload_codec`, two duplicate parsers
    to consolidate into one `JsonValue`-based implementation.
11. `http_mqtt_interface/internal/http_mqtt_interface_operations_connect_publish.cpp`
    + `..._subscriptions.cpp` — outgoing request body builders only;
    response parsing is already done.
12. `message/message_payload_codec.cpp` (+ `message_log_formatter.cpp`) —
    shared, highest impact, needs a performance check because it runs per
    MQTT message across all clients.
