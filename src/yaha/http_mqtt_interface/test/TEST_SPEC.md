# http_mqtt_interface test specification

## Scope

Unit tests for shared HTTP MQTT contract types and validation helpers.

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
