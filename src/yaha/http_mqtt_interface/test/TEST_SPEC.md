# http_mqtt_interface test specification

## Scope

Unit tests for shared HTTP MQTT contract types, validation helpers, and phase-2 dispatcher behavior.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `json_headers_match_phase1_defaults` | JSON header defaults are generated | call `makeStandardJsonHeaders` | contains expected `content-type`, `accept`, `accept-charset` |
| `text_headers_match_phase1_defaults` | Text header defaults are generated | call `makeStandardTextHeaders` | contains expected `content-type`, `accept`, `accept-charset` |
| `header_lookup_is_case_insensitive` | Header lookup tolerates mixed-case keys | headers with `Content-Type` and `PACKETID` | helper finds values using lowercase lookups |
| `require_header_throws_for_missing_key` | Required header is absent | empty headers and `packet` | throws deterministic missing-header error |
| `packet_id_parser_accepts_valid_decimal` | Packet id parser reads valid number | text `65535` | parsed value present and equal to `65535` |
| `packet_id_parser_rejects_invalid_text` | Packet id parser rejects malformed and overflow values | texts `-1`, `65a`, `70000` | parser returns nullopt |
| `resolve_version_uses_default_when_absent` | Version fallback is applied | headers without `version` | returns provided fallback |
| `require_json_object_payload_accepts_trimmed_object` | JSON object shape validation accepts trimmed object text | payload with spaces around object | no exception |
| `require_json_object_payload_rejects_non_object` | JSON object shape validation rejects arrays and plain text | payload `[1,2]` | throws deterministic payload-shape error |
| `dispatcher_get_version_defaults_to_0_0` | Dispatcher uses `0.0` fallback when header missing | headers without `version`, onPublish map with `0.0` | returns `0.0` |
| `dispatcher_get_version_throws_for_undefined_version` | Dispatcher rejects unsupported version | headers with `version=9.9`, onPublish map without `9.9` | throws `undefined version 9.9` |
| `interfaces_on_publish_dispatches_by_resolved_version` | Facade dispatches onPublish via header version | registry with `publishResponses[1.0]` and headers `version=1.0` | selected handler result is returned |
| `interfaces_on_publish_uses_0_0_fallback` | Facade onPublish supports missing version fallback | registry with `publishResponses[0.0]` and headers without version | `0.0` handler is used |
| `interfaces_publish_request_dispatches_by_explicit_version` | Request-side publish dispatch uses explicit version argument | registry with `publishRequests[1.0]` and call `publish(1.0,...)` | selected request builder result is returned |
| `interfaces_on_connect_and_on_disconnect_use_dispatcher_version` | Other `onX` methods use same dispatcher version rule | registry with `publishResponses[1.0]`, `connectResponses[1.0]`, `disconnectResponses[1.0]` | both handlers are selected for headers `version=1.0` |
