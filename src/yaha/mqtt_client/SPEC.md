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
- `unsubscribe(topic_filter)`
- `pollIncoming() -> optional<Message>`
- `ping()`
- `isConnected() -> bool`

The class is transport-agnostic; it does not contain socket/protocol code.

## Behavior

- On `run()`, injects component publish callback before processing incoming messages.
- Connect loop retries with `reconnectDelay` on failures.
- After each successful connect, fetches `component.getSubscriptions()` and subscribes all entries.
- During `close()`, unsubscribes active filters before transport disconnect for deterministic broker-side teardown.
- Inbound polling forwards only messages that match active subscriptions.
- Keep-alive sends `ping()` every `keepAliveInterval` while connected.
- On external disconnect (`isConnected() == false`), marks disconnected and reconnects.
- Exceptions raised by transport callbacks during the worker loop are treated as transient disconnects; the loop keeps running and retries connect after `reconnectDelay`.
- `close()` always ends with one `disconnect()` call if currently connected.
- `close()` ignores transport disconnect exceptions to preserve no-throw shutdown behavior.
- Lifecycle tracing is handled in this generic layer (`connect`, `connected`, `reconnect`, `reconnected`, `subscribe`, `unsubscribe`, `disconnect`, `connection lost`, `reconnecting`).
- Optional message tracing (`sent`/`recv`) is controlled by config flag `enableMessageTrace`.

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
