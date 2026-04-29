# mqtt_client — YahaMqttClient Session Driver

## Purpose

Provides the reusable MQTT session driver for YAHA components. The class wires one
`IMqttComponent` to transport callbacks, handles connect/reconnect, installs subscriptions,
forwards inbound messages, exposes publish, runs keep-alive pings, and supports graceful
shutdown.

## Public API

### Class `YahaMqttClient`

| Member | Signature | Notes |
|--------|-----------|-------|
| `Config` | nested struct | broker/session timing configuration |
| `Transport` | nested struct | callback bundle for MQTT transport operations |
| ctor | `YahaMqttClient(Config, IMqttComponent&, Transport)` | component is referenced, not owned |
| `run()` | `void()` | starts one background loop thread; no-op when already running |
| `close()` | `void()` | requests stop, joins thread, disconnects transport |
| `publish()` | `void(const Message&)` | validates message and forwards to transport publish callback |
| `isRunning()` | `bool() const` | true while background loop active |
| `isConnected()` | `bool() const` | true when loop currently considers transport connected |

## Transport callback contract

`Transport` contains function objects:
- `connect(config) -> bool`
- `disconnect()`
- `publish(message)`
- `subscribe(topic_filter, qos)`
- `pollIncoming() -> optional<Message>`
- `ping()`
- `isConnected() -> bool`

The class is transport-agnostic; it does not contain socket/protocol code.

## Behavior

- On `run()`, injects component publish callback before processing incoming messages.
- Connect loop retries with `reconnectDelay` on failures.
- After each successful connect, fetches `component.getSubscriptions()` and subscribes all entries.
- Inbound polling forwards only messages that match active subscriptions.
- Keep-alive sends `ping()` every `keepAliveInterval` while connected.
- On external disconnect (`isConnected() == false`), marks disconnected and reconnects.
- `close()` always ends with one `disconnect()` call if currently connected.

## Topic matching

Topic filter matching supports MQTT wildcards used by component subscriptions:
- `+` matches exactly one topic segment
- `#` matches remaining segments and must be last segment
- exact segment match otherwise

## Threading model

- One internal worker thread created by `run()`.
- State (`running`, `connected`, subscription map) guarded by a mutex.
- Component callback invocation happens on the worker thread.

## Files

| File | Role |
|------|------|
| `mqtt_client.h` | Public declarations |
| `mqtt_client.cpp` | Session loop implementation |
| `test/TEST_SPEC.md` | Unit-test specification |
| `test/mqtt_client_test.cpp` | Unit tests |
