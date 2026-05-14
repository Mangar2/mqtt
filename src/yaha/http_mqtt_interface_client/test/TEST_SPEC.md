# http_mqtt_interface_client test specification

## Scope

Unit tests for standalone HTTP MQTT interface client INI mapping behavior.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `load_http_mqtt_interface_client_config_defaults` | default behavior with empty ini | empty INI document | loader succeeds and keeps default values |
| `load_http_mqtt_interface_client_config_from_ini` | explicit field mapping | INI with all `[httpMqttInterface]` keys | loader succeeds and mapped values match |
| `load_http_mqtt_interface_client_config_reports_invalid_port` | numeric bounds validation | `listenerPort=70000` | loader fails with `httpMqttInterface.listenerPort` error |
| `load_http_mqtt_interface_client_config_reports_invalid_alias_flag` | bool parsing for publish.php toggle | `enablePublishPhpAlias=maybe` | loader fails with `httpMqttInterface.enablePublishPhpAlias` error |
| `load_http_mqtt_interface_client_config_reports_invalid_legacy_flag` | bool parsing for legacy response toggle | `useLegacyPhpResponse=invalid` | loader fails with `httpMqttInterface.useLegacyPhpResponse` error |
| `http_mqtt_interface_component_serves_endpoints_logs_publish_and_stops_on_signal` | runtime HTTP contract and ingress logging for browser clients via IMqttComponent coupling | GET/PUT/POST/OPTIONS requests to `/publish`, `/publish.php`, `/pubrel` | responses include CORS headers, publish requests are logged, and preflight returns `204` |
| `http_mqtt_interface_component_logs_broker_publish_error_when_ack_missing` | broker publish callback fails with ACK timeout and maps to deterministic 500 | POST `/publish` compatibility payload while generic publish callback throws timeout text | response is `500`, and logs include failed message fields plus no-ack detail |
| `http_mqtt_interface_component_returns_error_on_listen_failure` | invalid listener host produces deterministic startup failure | component runtime with invalid `listenerHost` | generic runtime throws `YahaError` startup failure |
| `http_mqtt_interface_component_recovers_across_repeated_broker_publish_failures` | repeated broker publish failures must not break long-running request handling | sequence of multiple POST `/publish` requests while publish callback alternates fail/success | responses alternate `500` then `204`, process keeps serving requests |
| `http_mqtt_interface_component_put_publish_failure_returns_500_and_logs` | native PUT publish handler maps dispatcher exceptions to deterministic internal error | PUT `/publish` without supported version headers | response is `500` and error log contains endpoint `/publish` |
| `http_mqtt_interface_component_put_pubrel_failure_returns_500_and_logs` | native PUT pubrel handler maps dispatcher exceptions to deterministic internal error | PUT `/pubrel` without supported version headers | response is `500` and error log contains endpoint `/pubrel` |
| `http_mqtt_interface_component_run_twice_and_close_without_run_is_safe` | component lifecycle methods are idempotent and safe under repeated calls | call `close()` before `run()`, then call `run()` twice and `close()` | no throw, listener lifecycle remains stable |
