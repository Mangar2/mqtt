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
