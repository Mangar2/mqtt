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
| `run_http_mqtt_interface_client_serves_endpoints_logs_publish_and_stops_on_signal` | runtime HTTP contract and ingress logging for browser clients | GET/PUT/POST/OPTIONS requests to `/publish`, `/publish.php`, `/pubrel` | responses include CORS headers, publish requests are logged, and preflight returns `204` |
| `run_http_mqtt_interface_client_serves_endpoints_logs_real_broker_forward_publish` | publish flow must log only after broker forwarding callback completed (ACK point) with resolved MQTT message fields | POST `/publish` and `/publish.php` with compatibility payload | log contains explicit broker-forward ACK entry including `topic=...` |
| `run_http_mqtt_interface_client_logs_broker_publish_error_when_ack_missing` | broker publish callback fails due missing ACK | POST `/publish` compatibility payload and callback throws timeout text | response is `500`, and logs include failed message fields (`topic`, `value`) plus no-ack detail |
| `run_http_mqtt_interface_client_put_publish_failure_returns_500_and_logs` | native PUT publish handler maps dispatcher exceptions to deterministic internal error | PUT `/publish` without supported version headers | response is `500` and error log contains endpoint `/publish` |
| `run_http_mqtt_interface_client_retries_broker_connect_after_initial_connect_failure` | broker connect failure must be retried on next publish request | first broker `connect` returns false, second `connect` succeeds | first response is `500`, second response is `204`, and connect callback is invoked twice |
| `run_http_mqtt_interface_client_put_pubrel_failure_returns_500_and_logs` | native PUT pubrel handler maps dispatcher exceptions to deterministic internal error | PUT `/pubrel` without supported version headers | response is `500` and error log contains endpoint `/pubrel` |
| `run_http_mqtt_interface_client_shutdown_tolerates_disconnect_throw` | transport disconnect exception at shutdown must not crash process | run with injected transport where disconnect throws after one successful publish | process exits with `0` and emits `broker_disconnect_failed` log |
