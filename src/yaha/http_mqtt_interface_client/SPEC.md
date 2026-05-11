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

### Function `tryLoadHttpMqttInterfaceClientConfigFromIni(...)`

Reads optional keys from section `[httpMqttInterface]`:

- `listenerHost`
- `listenerPort` (range `1..65535`)
- `enablePublishPhpAlias`
- `useLegacyPhpResponse`

Behavior:

- Missing keys keep defaults.
- Invalid numeric or boolean values return `false` and provide field-specific error text.

### Function `runHttpMqttInterfaceClient(...)`

Starts `httplib::Server` and wires handlers:

- `GET /health` returns `200` `ok`
- `PUT /publish` maps to native `HttpMqttInterfaces::onPublish`
- `PUT /pubrel` maps to native `HttpMqttInterfaces::onPubrel`
- `POST /publish` maps through phase-6 compatibility profile
- `POST /publish.php` maps through phase-6 compatibility profile
- `OPTIONS /publish`, `OPTIONS /publish.php`, and `OPTIONS /pubrel` return CORS preflight `204`

CORS headers on publish/pubrel responses:

- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Methods: POST, PUT, OPTIONS`
- `Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With`
- `Access-Control-Max-Age: 86400` on OPTIONS responses

Compatibility behavior delegates to `handlePublishCompatibilityRequest(...)` with runtime-configured mode:

- `Native` when `useLegacyPhpResponse=false`
- `LegacyPhp` when `useLegacyPhpResponse=true`

Signal handling:

- `SIGINT` and `SIGTERM` trigger `httplib::Server::stop()`.

## Files

| File | Role |
|------|------|
| `http_mqtt_interface_client_app.h` | Runtime config and API declarations |
| `http_mqtt_interface_client_app.cpp` | INI mapping and standalone HTTP server wiring |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/http_mqtt_interface_client_app_test.cpp` | Unit tests for INI mapping behavior |
