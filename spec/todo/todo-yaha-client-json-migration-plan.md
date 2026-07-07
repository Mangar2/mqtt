# TODO Plan: Replace Custom JSON Handling in YAHA Clients with src/json

## Scope

Several `src/yaha/*_client` modules parse or build JSON with their own hand-rolled
code instead of the generic `src/json` module (`JsonValue::parse` /
`JsonValue::stringify`, see `src/json/SPEC.md`). This plan inventories where that
custom JSON handling lives so it can be replaced module by module.

Goal: every client uses `JsonValue` for JSON parsing/serialization. Custom
recursive-descent parsers, manual escaping, and manual string-concatenation
JSON builders in client code should be removed.

## All 12 YAHA clients

There are exactly 12 YAHA client apps, one per `src/yaha_..._main.cpp` entry point.
Every one of them was checked for own JSON handling in its `src/yaha/*_client/`
directory.

| # | Main entry point | Client directory | Own JSON? | Action |
|---|---|---|---|---|
| 1 | `yaha_automationclient_main.cpp` | `automation_client/` | Yes | Migrate |
| 2 | `yaha_brokerconnectorclient_main.cpp` | `broker_connector_client/` | No | none |
| 3 | `yaha_filestoreclient_main.cpp` | `file_store_client/` | No | none |
| 4 | `yaha_httpmqttinterfaceclient_main.cpp` | `http_mqtt_interface_client/` | No (already uses `JsonValue`) | none |
| 5 | `yaha_msgstoreclient_main.cpp` | `message_store_client/` | Yes | Migrate |
| 6 | `yaha_opensensemapclient_main.cpp` | `opensensemap_client/` | No | none |
| 7 | `yaha_pushoverclient_main.cpp` | `pushover_client/` | No | none |
| 8 | `yaha_remoteserviceclient_main.cpp` | `remote_service_client/` | No | none |
| 9 | `yaha_rs485interfaceclient_main.cpp` | `rs485_interface_client/` | No | none |
| 10 | `yaha_serialdeviceclient_main.cpp` | `serial_device_client/` | No | none |
| 11 | `yaha_valueserviceclient_main.cpp` | `value_service_client/` | No | none |
| 12 | `yaha_zwaveclient_main.cpp` | `zwave_client/` | Yes | Migrate |

`mqtt_client/` is **not** one of the 12 clients (no `yaha_..._main.cpp` of its
own) — it is shared infrastructure used by all 12 clients, see the "shared
dependency" section below.

## 1. Clients with own JSON implementation — migration needed

- [x] **zwave_client** (`src/yaha/zwave_client/`, client #12)
  - `zwave_client_app.cpp` (~lines 462-1136): full custom recursive-descent
    parser for FileStore-persisted device settings —
    `parseJsonStringToken`, `parseJsonUnsignedToken`, `parseJsonDeviceField`,
    `parseJsonDeviceObject`, `parseJsonDevicesArray`, `parseJsonRootEntry`,
    `parseJsonRootEntries`, `parseJsonRootDevices` — plus a custom builder:
    `appendStringField`, `appendNumberField`, `appendOptionalNumberField`,
    `appendOptionalStringField`, `appendDeviceAsJson`,
    `serializeZwaveSettingsToJson`.
  - `zwave_client_app.h`: declares the public entry points
    `tryApplyZwaveDeviceSettingsFromJson(...)` and
    `serializeZwaveSettingsToJson(...)` — signatures may need to change if the
    payload is represented as `JsonValue` instead of raw `std::string`.
  - Biggest and clearest migration candidate: largest amount of duplicated
    parser/serializer logic among all clients.

- [x] **automation_client** (`src/yaha/automation_client/`, client #1)
  - `automation_rule_json.cpp` / `automation_rule_json.h`: custom
    `escapeJsonString` and a recursive `toJsonText` serializer that turns a
    `RuleTreeNode` into JSON text.
  - Parsing itself is delegated to `automation/rules_tree_json_reader.cpp` /
    `.h` (398 lines) — **not** located in `automation_client`, but it is the
    only parser backing `automation_client`'s `parseJsonNode`, and it
    duplicates a full recursive-descent JSON parser incl. UTF-8/escape
    handling that already exists in `src/json/json_value.cpp`. Must be
    migrated together with `automation_rule_json.cpp`, otherwise the
    duplicate parser just moves rather than disappears.

- [ ] **message_store_client** (`src/yaha/message_store_client/`, client #5)
  - `message_store_client_app.cpp`/`.h` themselves have no JSON code, but the
    HTTP interface they start is served by the `message_store` library, which
    is the part that actually converts data to/from JSON for HTTP callers.
    Same pattern as automation_client + `automation/rules_tree_json_reader`:
    the client is clean, the shared library behind it is not.
  - `message_store/message_store_json_parser.cpp` / `.h` (845 / 46 lines): a
    fully custom, hand-written parser for incoming HTTP request bodies —
    `parseSnapshotBody`, `parseSensorPostBody` — with no include of
    `json/json_value.h` at all.
  - `message_store/message_store.cpp` (~lines 459-546): hand-rolled
    string-concatenation JSON response builder returned to HTTP callers —
    `jsonStringLiteral`, `valueToJson`, `reasonsToJson`, `historyToJson`,
    `nodeToJson`, `nodesToJson`, `wrapPayloadObject`. Only individual string
    values are escaped via `JsonValue{...}.stringify()`
    (`jsonStringLiteral`); the surrounding object/array structure is built by
    hand instead of via `JsonValue::stringify()`.
  - Must migrate both `message_store_json_parser` and the response builder in
    `message_store.cpp` together — parsing and serialization are two ends of
    the same custom implementation.

## 2. Shared dependency used by (almost) every client — flagged separately

- [ ] **message/message_payload_codec.cpp** / `.h` (`src/yaha/message/`, 609 lines)
  - Not a `_client` directory itself, but implements the full custom JSON
    envelope codec used by `mqtt_client` for every published/subscribed
    message: `escapeJsonString`, `buildEnvelopePayload`, `parseValueToken`,
    `parseReasonArray`, `parseEnvelopePayload`, `validateEnvelopeShape`.
  - Because every `*_client` app sends/receives messages through
    `mqtt_client`, this codec is indirectly used by all 12 clients
    (automation_client, broker_connector_client, file_store_client,
    http_mqtt_interface_client, message_store_client, opensensemap_client,
    pushover_client, remote_service_client, rs485_interface_client,
    serial_device_client, value_service_client, zwave_client).
  - Treat as its own migration item, not a "client": it sits on the hot path
    for every MQTT message, so replacing it with `JsonValue` needs a
    performance check (parse/stringify cost per message) before rollout.

## 3. Clients checked — no own JSON implementation, no action needed

- **http_mqtt_interface_client** (client #4) — already uses `JsonValue` from
  `src/json` directly
  (`internal/http_mqtt_interface_client_app_constructors.cpp`,
  `internal/http_mqtt_interface_client_app_helpers.cpp`,
  `internal/http_mqtt_interface_client_app_internal.h`). Use this as the
  reference example for how the migrated clients should look.
- **opensensemap_client** (client #6), **pushover_client** (client #7) — the
  client-app files only reference the literal string `"application/json"` as
  an HTTP content-type header; they don't parse or build JSON themselves.
  - Note: their non-client sibling components
    `opensensemap/opensensemap_component.cpp` and
    `pushover/pushover_component.cpp` do own light JSON handling
    (`OpenSenseMapComponent::extractJsonMessage` does a manual
    `find("\"message\"")` field extraction, and the request body is built with
    `stream << R"({"value":)" << numericValue << '}'`). Out of scope here
    because these are not `_client` directories — revisit if the scope should
    widen to non-client components.
- **broker_connector_client** (client #2) — no JSON parsing/building inside
  `broker_connector_client_app.cpp`/`.h`.
- **file_store_client** (client #3) — no JSON parsing/building inside
  `file_store_client_app.cpp`/`.h`.
- **remote_service_client** (client #8) — no JSON parsing/building inside
  `remote_service_client_app.cpp`/`.h`.
- **rs485_interface_client** (client #9) — no JSON parsing/building inside
  `rs485_interface_client_app.cpp`/`.h`.
- **serial_device_client** (client #10) — no JSON parsing/building inside
  `serial_device_client_app.cpp`/`.h`.
- **value_service_client** (client #11) — no JSON parsing/building inside
  `value_service_client_app.cpp`/`.h`.

## Suggested order

1. `automation_client` + `automation/rules_tree_json_reader` — done
  (migrated to `src/json` `JsonValue` parse/stringify).
2. `zwave_client` — done
  (migrated to `src/json` `JsonValue` parse/stringify for settings sync).
3. `message_store_client` (via `message_store/message_store_json_parser` +
   `message_store.cpp` response builder) — self-contained, moderate size,
   HTTP request/response path, not per-MQTT-message hot path.
4. `message/message_payload_codec` — shared, highest impact, needs a
  performance check because it runs per MQTT message across all clients.
