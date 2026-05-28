# opensensemap — OpenSenseMap MQTT Bridge Component

## Purpose

Implements a YAHA `IMqttComponent` that forwards configured sensor MQTT values to
OpenSenseMap and publishes operation status to `$SYS/opensensemap/*` topics.

## Public API

### Struct `OpenSenseMapSensorConfig`

| Field | Type | Meaning |
|------|------|---------|
| `sensorName` | `std::string` | Human-readable sensor label |
| `sensorUnit` | `std::string` | Sensor unit text |
| `topicFilter` | `std::string` | MQTT topic mapped to this sensor |
| `sensorIdentifier` | `std::string` | OpenSenseMap sensor id |

### Struct `OpenSenseMapHttpResult`

| Field | Type | Meaning |
|------|------|---------|
| `statusCode` | `int` | HTTP response status |
| `payload` | `std::string` | Raw response body |
| `contentType` | `std::string` | Response content type header |

### Struct `OpenSenseMapConfig`

| Field | Type | Default | Meaning |
|------|------|---------|---------|
| `stationName` | `std::string` | empty | Optional station label |
| `boxIdentifier` | `std::string` | empty | OpenSenseMap senseBox id |
| `host` | `std::string` | `ingress.opensensemap.org` | OpenSenseMap host |
| `port` | `std::uint16_t` | `443` | OpenSenseMap port |
| `useTls` | `bool` | `true` | Request transport mode hint |
| `subscribeQos` | `Qos` | `Qos::AtLeastOnce` | Subscription QoS |
| `sensors` | `std::vector<OpenSenseMapSensorConfig>` | empty | Sensor mapping list |

### Type alias `OpenSenseMapRequestSender`

`std::function<OpenSenseMapHttpResult(const std::string& requestPath, const std::string& requestPayload)>`

### Class `OpenSenseMapComponent`

Implements `IMqttComponent` behavior:

- `getSubscriptions()` returns all configured sensor topic mappings with configured QoS.
- `handleMessage(...)` resolves sensor by topic, converts value to number, posts payload
  `{"value": <number>}` to OpenSenseMap, and publishes status result message.
- `run()` enables message processing.
- `close()` disables message processing.
- `setPublishCallback(...)` stores runtime MQTT publish callback.

## Status publish behavior

- Success (`HTTP 201`) -> topic `$SYS/opensensemap/success`
- Any other status or local error -> topic `$SYS/opensensemap/error`
- Status payload value is numeric HTTP-like status code.
- Outbound status message reason chain appends original inbound reasons and one result reason.

## Files

| File | Role |
|------|------|
| `opensensemap_component.h` | Public config/contracts and component declaration |
| `opensensemap_component.cpp` | Component implementation |
| `test/TEST_SPEC.md` | Unit-test specification |
| `test/opensensemap_component_test.cpp` | Unit tests |
