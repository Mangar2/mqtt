# http_mqtt_interface_client — HTTP MQTT Domain Component

## Purpose

Provides domain component and config mapping for standalone HTTP MQTT interface.
Domain component implements `IMqttComponent` only.
Generic MQTT client and generic runtime orchestrator own broker communication and signal lifecycle.

## Public API

### Struct `HttpMqttInterfaceClientConfig`

| Field | Type | Default | Meaning |
|------|------|---------|---------|
| `listenerHost` | `std::string` | `127.0.0.1` | HTTP bind host |
| `listenerPort` | `std::uint16_t` | `8092` | HTTP bind port |
| `enablePublishPhpAlias` | `bool` | `true` | Enables `POST /publish.php` compatibility route |
| `useLegacyPhpResponse` | `bool` | `false` | Enables legacy PHP response conversion mode |
| `mqttConfig` | `YahaMqttClient::Config` | defaults from MQTT client config | Generic MQTT client session config |

### Class `HttpMqttInterfaceClientComponent`

Implements `IMqttComponent` for HTTP MQTT domain behavior.

Behavior:

- `getSubscriptions()` returns empty map.
- `handleMessage(...)` is no-op.
- `run()` starts HTTP listener in background thread.
- `close()` stops listener and joins thread.
- `setPublishCallback(...)` stores callback injected by generic MQTT client.

Publish forwarding path uses only IMqttComponent publish callback contract.
No direct broker transport callback bundle is owned in this module.

### Function `tryLoadHttpMqttInterfaceClientConfigFromIni(...)`

Reads optional keys from section `[httpMqttInterface]`:

- `listenerHost`
- `listenerPort` (range `1..65535`)
- `enablePublishPhpAlias`
- `useLegacyPhpResponse`

Also delegates MQTT client config parsing to shared MQTT config loader:

- section `[mqtt]` and related runtime keys accepted by `tryLoadMqttClientConfigFromIni(...)`

Behavior:

- missing keys keep defaults
- invalid numeric or boolean values return `false` with field-specific error text

## HTTP Endpoint Behavior

Component starts `httplib::Server` and wires:

- `GET /health` returns `200` `ok`
- `PUT /publish` maps to native `HttpMqttInterfaces::onPublish`
- `PUT /pubrel` maps to native `HttpMqttInterfaces::onPubrel`
- `POST /publish` maps through compatibility profile
- `POST /publish.php` maps through compatibility profile
- `OPTIONS /publish`, `OPTIONS /publish.php`, `OPTIONS /pubrel` return CORS preflight `204`

Publish ingress logging:

- each handled publish request writes one stdout line
- includes method, endpoint, and `version` header when present

Publish broker-forward logging:

- successful callback publish emits `broker_publish_ack` with message fields
- failed callback publish emits `broker_publish_failed` with message fields and error text
- timeout-style failures add `detail=message_was_sent_but_broker_reported_no_ack`

Native PUT error mapping:

- `PUT /publish` and `PUT /pubrel` wrap dispatcher exceptions into deterministic internal error response (`500`, JSON `{"error":"internal_error"}`)
- failed PUT requests emit `publish_request_failed` with endpoint and reason

Compatibility error mapping:

- callback failures and mapping exceptions produce deterministic internal error response (`500`, JSON `{"error":"internal_error"}`)
- request-level failure logs contain endpoint and reason

CORS headers on publish/pubrel responses:

- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Methods: POST, PUT, OPTIONS`
- `Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With`
- `Access-Control-Max-Age: 86400` on OPTIONS responses

## Ownership Boundaries

- generic MQTT client owns connect disconnect reconnect keepalive subscribe unsubscribe publish delivery status
- generic runtime owns signal handling and shutdown choreography
- this domain component owns only HTTP request mapping and domain-level compatibility behavior

## Files

| File | Role |
|------|------|
| `http_mqtt_interface_client_app.h` | Runtime config and domain component declarations |
| `http_mqtt_interface_client_app.cpp` | INI mapping and IMqttComponent implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/http_mqtt_interface_client_app_test.cpp` | Unit tests for config and component runtime behavior |
