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
| `connect_v1_request_sets_headers_and_keepalive_default` | Connect request builder applies v1 defaults | call `connect("1.0", ...)` without keepAlive | payload contains `keepAlive:0` and `version=1.0` header |
| `connect_v1_result_check_accepts_valid_connack` | Connect result validator accepts valid response | status 200, `packet=connack`, payload with present/token | result check does not throw |
| `connect_v1_result_check_throws_for_mqtt_error_code` | Connect validator maps mqtt error codes | response payload with `mqttcode=4` | result check throws mapped connect error |
| `disconnect_v1_request_and_on_disconnect_response` | Disconnect request and response contract | call `disconnect` and `onDisconnect` for `1.0` | request has version header, response is `204` and empty payload |
| `publish_v1_request_and_result_check_qos1` | Publish v1 request and qos1 ack validation | qos1 message and response `packet=puback` | result check does not throw |
| `publish_v1_request_preserves_raw_payload_without_rebuild` | Publish v1 request keeps full mapped payload untouched when raw payload exists | message with `rawPayload` containing full JSON envelope | request payload equals `rawPayload` exactly without serializer rewrite |
| `on_publish_v1_maps_ack_headers_by_qos` | onPublish maps ack packet by qos | qos headers `1` and `2` | returns `puback` for qos1 and `pubrec` for qos2 |
| `pubrel_v1_request_result_and_response` | Pubrel builder and response contract | call `pubrel`, validate `packet=pubcomp`, call `onPubrel` | request and response satisfy v1 rules |
| `subscribe_v1_request_result_and_response` | Subscribe builder and validation contract | subscribe response with qos array and matching packetid | result check passes and onSubscribe returns suback payload |
| `unsubscribe_v1_accepts_204_empty_payload` | Unsubscribe backward-compatibility path | status `204`, empty payload | result check passes |
| `unsubscribe_v1_result_and_response_with_codes` | Unsubscribe return-code validation and response contract | status `200` with `[0,17]` | result check passes and onUnsubscribe returns `unsuback` |
| `compat_publish_post_form_maps_to_publish_v1_defaults` | Browser compat form fields map to native publish v1 with defaults | `POST /publish` with `topic` and `value` fields | forwarded request has decoded topic plus default qos=1, retain=0, and appended publish-ingress reason `Request by User` |
| `compat_publish_falls_back_to_json_body_when_topic_missing` | Topic/value are read from JSON body when form/query topic is missing | `POST /publish` with empty fields and JSON body | forwarded request payload contains topic/value from body |
| `compat_publish_php_alias_disabled_returns_405` | Deployment can disable `/publish.php` alias | `POST /publish.php` with alias disabled | response status is `405` |
| `compat_publish_php_alias_enabled_forwards_request` | Legacy alias route forwards when enabled | `POST /publish.php` with alias enabled | forwarder is invoked and response is native success |
| `compat_publish_legacy_mode_wraps_downstream_payload` | Legacy mode returns PHP-compatible wrapper payload | compat request with legacy mode and downstream `204` payload | response status `200` and payload is JSON stringified downstream payload |
| `compat_publish_invalid_json_returns_400` | Invalid JSON payload is rejected | `POST /publish` with malformed JSON body and missing topic field | response status is `400` |
| `compat_publish_forwarder_exception_propagates_to_caller` | Forwarder runtime error is not swallowed inside compatibility layer | `POST /publish` with valid mapped fields and forwarder throws | function throws runtime error to caller |
| `compat_publish_missing_topic_returns_400` | Missing topic is rejected after all extraction fallbacks | `POST /publish` with empty fields and empty body | response status is `400` and request is not forwarded |
| `phase7_e2e_sequence_connect_to_disconnect_validates_contract` | Full contract sequence validates request checks and onX response envelopes | connect -> subscribe -> publish -> pubrel -> unsubscribe -> disconnect | each step passes resultCheck and packet/packetid contracts are enforced |
