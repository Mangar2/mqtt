# http_mqtt_interface — HTTP MQTT Contracts and Version Dispatcher

## Purpose

Provides shared contract types, low-level helpers, and the phase-2 version dispatcher
for HTTP MQTT interface operations.
This module still contains no operation-specific protocol implementation. It defines
reusable data structures, standard headers, deterministic validation helpers, and the
top-level `HttpMqttInterfaces` facade shell used to dispatch versioned handlers.

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

## Constraints and behavior

- Header key lookup is case-insensitive through lowercase normalization.
- Packet id parser accepts only complete unsigned integer text in range `0..65535`.
- `requireJsonObjectPayload` checks normalized payload shape only (`{...}`) and does not perform full JSON decoding.
- Error paths use deterministic `std::runtime_error` messages for stable operation diagnostics.
- Version dispatch fallback is `0.0` when `version` header is missing or empty.
- Undefined dispatch versions always throw `undefined version <version>`.

## Files

| File | Role |
|------|------|
| `http_mqtt_interface_contracts.h` | Public contracts and helper declarations |
| `http_mqtt_interface_contracts.cpp` | Shared helper implementations |
| `http_mqtt_interface_dispatcher.h` | Versioned handler registry and `HttpMqttInterfaces` facade declarations |
| `http_mqtt_interface_dispatcher.cpp` | Dispatcher and facade implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/http_mqtt_interface_contracts_test.cpp` | Unit tests for phase 1 contracts and helpers |
| `test/http_mqtt_interface_dispatcher_test.cpp` | Unit tests for phase 2 dispatcher and facade shell |
