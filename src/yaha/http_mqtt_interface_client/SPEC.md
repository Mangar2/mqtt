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

Additional command behavior:

- component also owns a session manager that maintains multiple broker-backed sessions
	addressed by tokens returned from `PUT /connect`
- session manager uses one broker transport per connected HTTP client session
	and forwards command operations to broker transport directly

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
- invalid recoverable numeric/boolean values keep defaults, emit deterministic
	warning log to `std::cerr`, and still return success
- invalid MQTT sub-loader values keep MQTT defaults, emit warning, and do not
	abort config loading

## HTTP Endpoint Behavior

Component starts `httplib::Server` and wires:

- `GET /health` returns `200` `ok`
- `PUT /connect` creates a broker-backed session and returns token pair
- `PUT /subscribe` forwards topic subscriptions to token-bound broker session
- `PUT /unsubscribe` forwards topic unsubscriptions to token-bound broker session
- `PUT /receive` polls one message from token-bound broker session
- `PUT /pingreq` forwards keepalive ping to token-bound broker session
- `PUT /disconnect` closes token-bound broker session
- `PUT /publish` maps to native `HttpMqttInterfaces::onPublish`
- `PUT /pubrel` maps to native `HttpMqttInterfaces::onPubrel`
- `POST /publish` maps through compatibility profile
- `POST /publish.php` maps through compatibility profile
- `OPTIONS /publish`, `OPTIONS /publish.php`, `OPTIONS /pubrel`, `OPTIONS /connect`,
  `OPTIONS /subscribe`, `OPTIONS /unsubscribe`, `OPTIONS /receive`, `OPTIONS /pingreq`,
  `OPTIONS /disconnect` return CORS preflight `204`

Publish ingress logging:

- each handled publish request writes one stdout line
- includes method, endpoint, and `version` header when present

Publish broker-forward logging:

- successful callback publish emits one shared outgoing message log line plus `event=broker_publish_ack`
- failed callback publish emits one shared outgoing message log line plus `event=broker_publish_failed` and escaped error text
- broker publish logs include full message reason chain (`reason=[...]`) from mapped incoming publish payload
- timeout-style failures add `detail=message_was_sent_but_broker_reported_no_ack`
- compatibility publish keeps existing behavior: if token does not match a managed
	session, it continues to use injected generic MQTT publish callback unchanged
- if token matches a managed session, compatibility publish is forwarded through the
	corresponding token-bound broker session

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

- generic MQTT client still owns the legacy single-session publish callback path used by browser compatibility flows
- session manager owns token-bound multi-session connect subscribe publish receive ping unsubscribe disconnect forwarding
- generic runtime owns signal handling and shutdown choreography
- this domain component owns only HTTP request mapping and domain-level compatibility behavior

## Files

| File | Role |
|------|------|
| `http_mqtt_interface_client_app.h` | Runtime config and domain component declarations |
| `http_mqtt_interface_client_app.cpp` | INI mapping and IMqttComponent implementation |
| `http_mqtt_session_manager.h` | Token-bound broker session manager API |
| `http_mqtt_session_manager.cpp` | Token-bound broker session forwarding implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/http_mqtt_interface_client_app_test.cpp` | Unit tests for config and component runtime behavior |
