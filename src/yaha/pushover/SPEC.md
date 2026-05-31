# pushover — Pushover Incident Bridge Component

## Purpose

Implements a YAHA `IMqttComponent` that forwards subscribed incident messages to
Pushover devices and publishes operation status to `$MONITOR/pushover/*` topics.

## Public API

### Struct `PushoverSubscriptionConfig`

| Field | Type | Meaning |
|------|------|---------|
| `topicFilter` | `std::string` | MQTT topic filter to subscribe |
| `qos` | `Qos` | Requested QoS for this filter |

### Struct `PushoverHttpResult`

| Field | Type | Meaning |
|------|------|---------|
| `statusCode` | `int` | HTTP response status |
| `payload` | `std::string` | Raw response body |
| `contentType` | `std::string` | HTTP response content-type |

### Struct `PushoverConfig`

| Field | Type | Default | Meaning |
|------|------|---------|---------|
| `host` | `std::string` | `api.pushover.net` | Pushover host |
| `path` | `std::string` | `/1/messages.json` | Pushover endpoint path |
| `port` | `std::uint16_t` | `443` | Pushover endpoint port |
| `token` | `std::string` | empty | Pushover application token |
| `user` | `std::string` | empty | Pushover user key |
| `devices` | `std::vector<std::string>` | empty | Target devices |
| `subscriptions` | `std::vector<PushoverSubscriptionConfig>` | empty | MQTT subscriptions |

### Type alias `PushoverRequestSender`

`std::function<PushoverHttpResult(const std::string& requestPath, const std::string& requestPayload)>`

### Class `PushoverComponent`

Implements `IMqttComponent` behavior:

- `getSubscriptions()` returns configured topic filters from `subscriptions`.
- `handleMessage(...)` builds one Pushover request per configured device and publishes
  one status message per request.
- `run()` enables message processing.
- `close()` disables message processing.
- `setPublishCallback(...)` stores runtime MQTT publish callback.

## Request and status behavior

- Request payload fields: `token`, `user`, `message`, `priority`, `title`, `device`.
- Priority mapping:
  - message value `"alert"` -> priority `1`
  - every other value -> priority `-1`
- Message text mapping:
  - first reason with timestamp -> `<timestamp>: <message>`
  - no reason -> `no information`

Status publish behavior:

- success (`HTTP < 300`) -> topic `$MONITOR/pushover/success`
- error (`HTTP >= 300` or local failure) -> topic `$MONITOR/pushover/error`
- status payload value is numeric HTTP-like status code.
- outbound status reason appends original inbound reasons plus one Pushover result reason.

## Files

| File | Role |
|------|------|
| `pushover_component.h` | Public config/contracts and component declaration |
| `pushover_component.cpp` | Component implementation |
| `test/TEST_SPEC.md` | Unit-test specification |
| `test/pushover_component_test.cpp` | Unit tests |
