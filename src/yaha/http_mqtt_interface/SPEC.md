# http_mqtt_interface — HTTP MQTT Contracts, Dispatcher, and V1 Operations

## Purpose

Provides shared contract types, low-level helpers, the version dispatcher, and version 1.0
operation implementations for HTTP MQTT interface processing.
The module now includes concrete v1 handlers for connect/disconnect, publish/pubrel,
subscribe/unsubscribe, plus browser compatibility mapping for `POST /publish` and
`POST /publish.php` replacement flows.

## Public API

### Type aliases

| Symbol | Type | Meaning |
|--------|------|---------|
| `HttpMqttHeaders` | `std::map<std::string, std::string>` | HTTP header map for request/response contracts |
| `HttpMqttTopics` | `SubscriptionMap` | Topic->QoS mapping used by subscribe/unsubscribe |
| `HttpMqttResultCheck` | `std::function<void(const HttpMqttResult&)>` | Response validator callback signature |

### Struct `HttpMqttResult`

| Field | Type | Meaning |
|-------|------|---------|
| `statusCode` | `int` | HTTP status code |
| `headers` | `HttpMqttHeaders` | Response headers |
| `payload` | `std::string` | Serialized response payload |
| `present` | `std::optional<std::uint8_t>` | Optional connect session-present flag |
| `token` | `std::optional<std::string>` | Optional token payload |
| `runtime` | `std::optional<double>` | Optional runtime metric |
| `packetId` | `std::optional<std::uint16_t>` | Optional packet id |

### Struct `HttpMqttRequestData`

| Field | Type | Meaning |
|-------|------|---------|
| `headers` | `HttpMqttHeaders` | Request headers |
| `payload` | `std::string` | Serialized request payload |
| `resultCheck` | `HttpMqttResultCheck` | Operation-specific response check |

### Functions

| Function | Signature | Behavior |
|----------|-----------|----------|
| `makeStandardJsonHeaders` | `HttpMqttHeaders()` | Returns standard JSON headers (`content-type`, `accept`, `accept-charset`) |
| `makeStandardTextHeaders` | `HttpMqttHeaders()` | Returns standard text headers (`content-type`, `accept`, `accept-charset`) |
| `normalizeHeaderKeys` | `HttpMqttHeaders(const HttpMqttHeaders&)` | Lowercases header keys for case-insensitive lookup |
| `tryReadHeaderValue` | `std::optional<std::string>(const HttpMqttHeaders&, std::string_view)` | Optional header value lookup |
| `headerValueStartsWith` | `bool(const HttpMqttHeaders&, std::string_view, std::string_view)` | Prefix check helper for header values |
| `requireHeaderValue` | `std::string(const HttpMqttHeaders&, std::string_view)` | Throws if required header is missing |
| `tryParsePacketId` | `std::optional<std::uint16_t>(std::string_view)` | Parses packet id text to uint16 |
| `tryReadPacketIdHeader` | `std::optional<std::uint16_t>(const HttpMqttHeaders&)` | Reads and parses packet id header |
| `resolveVersion` | `std::string(const HttpMqttHeaders&, std::string_view)` | Returns `version` header or fallback default |
| `requireJsonObjectPayload` | `void(std::string_view, std::string_view)` | Throws when payload is not JSON object shaped |
| `getVersion` | `std::string(const HttpMqttHeaders&, const HttpMqttPublishResponseHandlerMap&)` | Resolves version with default `0.0` and throws `undefined version <version>` if missing in onPublish map |

### Phase-2 facade shell

Structs introduced for facade signatures:

- `HttpMqttConnectTokens`
- `HttpMqttConnectResult`
- `HttpMqttConnectOptions`
- `HttpMqttPublishOptions`
- `HttpMqttPubrelOptions`
- `HttpMqttInterfaceHandlerRegistry`

Facade class:

- `HttpMqttInterfaces`
	- request-side methods:
		- `publish(version, options)`
		- `pubrel(version, options)`
		- `subscribe(version, topics, clientId, packetId)`
		- `unsubscribe(version, topics, clientId, packetId)`
		- `connect(version, options)`
		- `disconnect(version, clientId)`
	- response-side methods:
		- `onPublish(headers)`
		- `onPubrel(headers)`
		- `onSubscribe(headers, result)`
		- `onUnsubscribe(headers, result)`
		- `onConnect(headers, payload)`
		- `onDisconnect(headers)`

Dispatch behavior:

- all `onX` methods resolve version through `getVersion(...)`
- if resolved version is missing in `publishResponses`, throw `undefined version <version>`
- request-side methods dispatch by explicit version argument and throw the same undefined-version error for missing handlers

### Phase-3/4/5 operation implementation

Public factory functions:

- `makeHttpMqttInterfaceHandlerRegistryV1()`
- `makeHttpMqttInterfacesV1()`

Public compatibility API:

- `HttpMqttPublishCompatibilityResponseMode`
- `HttpMqttPublishCompatibilityConfig`
- `HttpMqttPublishCompatibilityRequest`
- `handlePublishCompatibilityRequest(...)`

Implemented version `1.0` operation pairs:

- connect / onConnect
- disconnect / onDisconnect
- publish / onPublish
- pubrel / onPubrel
- subscribe / onSubscribe
- unsubscribe / onUnsubscribe

Implemented behavior highlights:

- Connect request forces `keepAlive=0` when option is absent.
- Connect result validation enforces status, headers, payload shape, present flag, mqttcode rules, and token tuple.
- Publish request forwards only token and `message` fields (`topic`, `value`, `reason`) with qos/dup/retain headers.
- Publish result validation enforces qos-dependent ack packet rules and optional packetid echo.
- Pubrel response returns `packet=pubcomp` and optional packetid echo.
- Subscribe and unsubscribe request builders include packetid headers and topic maps.
- Unsubscribe result validation supports backward-compatible `204` with empty payload.

### Phase-6 browser compatibility profile

Implemented compatibility behavior:

- accepted routes:
	- `POST /publish`
	- `POST /publish.php` (switchable with `enablePublishPhpAlias`)
	- `PUT /publish`
- extraction order:
	- reads `topic`, `value`, `reason`, `qos`, `retain` from form/query fields first
	- falls back to JSON body parsing when topic is missing
- topic normalization decodes `%2F` and `%2f` to `/`
- defaults:
	- `qos=1`
	- `retain=false`
	- auto reason entry `Request by browser` with auto-generated ISO-8601 timestamp when reason is absent
- mapping:
	- compatibility input is translated to native Publish 1.0 request data through `HttpMqttInterfaces::publish("1.0", ...)`
- response modes:
	- `Native`: downstream `204` response is forwarded unchanged
	- `LegacyPhp`: returns `200` with JSON-stringified downstream payload string
- error mapping:
	- `400` for invalid JSON or missing topic
	- `405` for unsupported method/route
	- `500` for downstream/internal failures

## Constraints and behavior

- Header key lookup is case-insensitive through lowercase normalization.
- Packet id parser accepts only complete unsigned integer text in range `0..65535`.
- `requireJsonObjectPayload` checks normalized payload shape only (`{...}`) and does not perform full JSON decoding.
- Error paths use deterministic `std::runtime_error` messages for stable operation diagnostics.
- Version dispatch fallback is `0.0` when `version` header is missing or empty.
- Undefined dispatch versions always throw `undefined version <version>`.
- V1 operation factory wires only version `1.0`; fallback `0.0` remains undefined until explicitly added.
- Compatibility JSON parsing is strict for `reason`, `qos`, and `retain` tokens; malformed tokens return `400`.

## Files

| File | Role |
|------|------|
| `http_mqtt_interface_contracts.h` | Public contracts and helper declarations |
| `http_mqtt_interface_contracts.cpp` | Shared helper implementations |
| `http_mqtt_interface_dispatcher.h` | Versioned handler registry and `HttpMqttInterfaces` facade declarations |
| `http_mqtt_interface_dispatcher.cpp` | Dispatcher and facade implementation |
| `http_mqtt_interface_operations.h` | V1 operation factory declarations and phase-6 compatibility API |
| `http_mqtt_interface_operations.cpp` | V1 operation builders, validators, response handlers, and compatibility mapping |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/http_mqtt_interface_contracts_test.cpp` | Unit tests for phase 1 contracts and helpers |
| `test/http_mqtt_interface_dispatcher_test.cpp` | Unit tests for phase 2 dispatcher and facade shell |
| `test/http_mqtt_interface_operations_test.cpp` | Unit tests for phase 3/4/5 handlers and phase-6 compatibility profile |
