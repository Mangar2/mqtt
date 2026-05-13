# http_mqtt_interface_client — Standalone HTTP MQTT Interface Runtime

## Purpose

Provides a standalone executable runtime for the HTTP MQTT interface module.
The runtime exposes HTTP endpoints for publish/pubrel processing and browser
compatibility mapping without changing the existing broker connector process.

## Public API

### Struct `HttpMqttInterfaceClientConfig`

| Field | Type | Default | Meaning |
|------|------|---------|---------|
| `listenerHost` | `std::string` | `127.0.0.1` | HTTP bind host |
| `listenerPort` | `std::uint16_t` | `8092` | HTTP bind port |
| `enablePublishPhpAlias` | `bool` | `true` | Enables `POST /publish.php` compatibility route |
| `useLegacyPhpResponse` | `bool` | `false` | Enables legacy PHP response conversion mode |
| `mqttConfig` | `YahaMqttClient::Config` | defaults from MQTT client config | Broker publish transport settings used for compatibility publish forwarding |

### Function `tryLoadHttpMqttInterfaceClientConfigFromIni(...)`

Reads optional keys from section `[httpMqttInterface]`:

- `listenerHost`
- `listenerPort` (range `1..65535`)
- `enablePublishPhpAlias`
- `useLegacyPhpResponse`

Also delegates MQTT client config parsing to the shared MQTT config loader:

- section `[mqtt]` and related MQTT runtime keys accepted by `tryLoadMqttClientConfigFromIni(...)`

Behavior:

- Missing keys keep defaults.
- Invalid numeric or boolean values return `false` and provide field-specific error text.

### Function `runHttpMqttInterfaceClient(...)`

Two overloads are provided:

- `runHttpMqttInterfaceClient(config)`
- `runHttpMqttInterfaceClient(config, publishToBroker)`

The second overload is used for explicit injection/testing of the broker-forward callback.

Starts `httplib::Server` and wires handlers:

- `GET /health` returns `200` `ok`
- `PUT /publish` maps to native `HttpMqttInterfaces::onPublish`
- `PUT /pubrel` maps to native `HttpMqttInterfaces::onPubrel`
- `POST /publish` maps through phase-6 compatibility profile
- `POST /publish.php` maps through phase-6 compatibility profile
- `OPTIONS /publish`, `OPTIONS /publish.php`, and `OPTIONS /pubrel` return CORS preflight `204`

Publish ingress logging:

- every handled publish request writes one stdout line before dispatch
- logged requests include the request method, endpoint, and `version` header when present

Publish broker-forward logging:

- each compatibility publish emits one `broker_publish_ack` log line only after the broker-forward callback returned successfully
- line contains full MQTT message transport fields: `topic`, `qos`, `retain`, `dup`, optional `packetid`, and `value`
- callback failures emit one `broker_publish_failed` error line with the same message fields plus the error text
- timeout-style broker ACK failures add `detail=message_was_sent_but_broker_reported_no_ack`
- broker transport disconnect failures on shutdown emit `broker_disconnect_failed` and do not crash the runtime

Native PUT error mapping:

- `PUT /publish` and `PUT /pubrel` wrap dispatcher exceptions into deterministic internal error response (`500`, JSON `{"error":"internal_error"}`)
- failed PUT requests emit one request-level `publish_request_failed` error line with endpoint and reason

CORS headers on publish/pubrel responses:

- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Methods: POST, PUT, OPTIONS`
- `Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With`
- `Access-Control-Max-Age: 86400` on OPTIONS responses

Compatibility behavior delegates to `handlePublishCompatibilityRequest(...)` with runtime-configured mode:

- `Native` when `useLegacyPhpResponse=false`
- `LegacyPhp` when `useLegacyPhpResponse=true`

For compatibility publishes, the mapped `Message` is forwarded through the broker publish callback before the HTTP ack response is generated.

If broker forwarding fails during compatibility publish processing, the request is handled as internal error (`500`, JSON `{\"error\":\"internal_error\"}`) and broker publish failure details are logged with message fields.

If compatibility publish processing ends in a 5xx compatibility result, one request-level error line is logged with endpoint and failure details.

If a compatibility request handler throws past the mapping layer, one request-level error line is logged with endpoint and exception text.

Signal handling:

- `SIGINT` and `SIGTERM` trigger `httplib::Server::stop()`.

## Files

| File | Role |
|------|------|
| `http_mqtt_interface_client_app.h` | Runtime config and API declarations |
| `http_mqtt_interface_client_app.cpp` | INI mapping and standalone HTTP server wiring |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/http_mqtt_interface_client_app_test.cpp` | Unit tests for INI mapping behavior |
