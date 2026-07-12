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
| `minUploadIntervalSeconds` | `std::uint32_t` | Minimum time between uploads for this sensor; `0` disables guard |

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
| `logIncomingMessages` | `bool` | `false` | Logs every incoming message (topic/value/reason) to stdout in broker-format, regardless of guard/error outcome |

### Type alias `OpenSenseMapRequestSender`

`std::function<OpenSenseMapHttpResult(const std::string& requestPath, const std::string& requestPayload)>`

### Class `OpenSenseMapComponent`

Implements `IMqttComponent` behavior:

- `getSubscriptions()` returns all configured sensor topic mappings with configured QoS.
- `handleMessage(...)` logs the incoming message first (if `logIncomingMessages` is enabled),
  then resolves sensor by topic, converts value to number, posts payload
  `{"value": <number>}` to OpenSenseMap, and publishes status result message.
- If a sensor has `minUploadIntervalSeconds > 0`, uploads are rate-guarded per sensor id:
  - the first message uploads immediately
  - messages arriving before the configured interval elapsed are ignored
  - ignored messages are not queued, persisted, or retried
  - ignored messages write one warning log line and do not publish `$MONITOR/opensensemap/*` status
- `run()` enables message processing.
- `close()` disables message processing.
- `setPublishCallback(...)` stores runtime MQTT publish callback.

## Log tag

All stdout/stderr log lines emitted by this component are prefixed `opensensemap_client` (not
`opensensemap`) so they are unambiguously identifiable as coming from this local client process,
not from the remote OpenSenseMap service (e.g. `opensensemap_client[warn] ... action=ignore`,
`opensensemap_client <- <topic> : <value>`).

## Status publish behavior

- Success (`HTTP 201`) -> topic `$MONITOR/opensensemap/success`
- Any other status or local error -> topic `$MONITOR/opensensemap/error`
- Status payload value is numeric HTTP-like status code.
- Outbound status message reason chain appends original inbound reasons and one result reason.

## Files

| File | Role |
|------|------|
| `opensensemap_component.h` | Public config/contracts and component declaration |
| `opensensemap_component.cpp` | Component implementation |
| `test/TEST_SPEC.md` | Unit-test specification |
| `test/opensensemap_component_test.cpp` | Unit tests |
