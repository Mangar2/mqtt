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

- each compatibility publish emits one log line exactly at the broker-forward call site
- line contains full MQTT message transport fields: `topic`, `qos`, `retain`, `dup`, optional `packetid`, and `value`

CORS headers on publish/pubrel responses:

- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Methods: POST, PUT, OPTIONS`
- `Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With`
- `Access-Control-Max-Age: 86400` on OPTIONS responses

Compatibility behavior delegates to `handlePublishCompatibilityRequest(...)` with runtime-configured mode:

- `Native` when `useLegacyPhpResponse=false`
- `LegacyPhp` when `useLegacyPhpResponse=true`

For compatibility publishes, the mapped `Message` is forwarded through the broker publish callback before the HTTP ack response is generated.

Signal handling:

- `SIGINT` and `SIGTERM` trigger `httplib::Server::stop()`.

## Files

| File | Role |
|------|------|
| `http_mqtt_interface_client_app.h` | Runtime config and API declarations |
| `http_mqtt_interface_client_app.cpp` | INI mapping and standalone HTTP server wiring |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/http_mqtt_interface_client_app_test.cpp` | Unit tests for INI mapping behavior |
