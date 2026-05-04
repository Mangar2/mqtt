# http_mqtt_interface — Shared HTTP MQTT Interface Contracts

## Purpose

Provides shared contract types and low-level helpers for HTTP MQTT interface operations.
This module contains no operation-specific logic yet. It defines reusable data structures,
standard headers, and deterministic validation helpers used by connect/disconnect/publish/
pubrel/subscribe/unsubscribe implementations.

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

## Constraints and behavior

- Header key lookup is case-insensitive through lowercase normalization.
- Packet id parser accepts only complete unsigned integer text in range `0..65535`.
- `requireJsonObjectPayload` checks normalized payload shape only (`{...}`) and does not perform full JSON decoding.
- Error paths use deterministic `std::runtime_error` messages for stable operation diagnostics.

## Files

| File | Role |
|------|------|
| `http_mqtt_interface_contracts.h` | Public contracts and helper declarations |
| `http_mqtt_interface_contracts.cpp` | Shared helper implementations |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/http_mqtt_interface_contracts_test.cpp` | Unit tests for phase 1 contracts and helpers |
