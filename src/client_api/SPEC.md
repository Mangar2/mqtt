# client_api — Public client interfaces (Steps 23, 24, 25)

Blocking and non-blocking public client facades that wrap lower-level client
components.
Depends on `client/` and `data_model/`.

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `sync_client.h/.cpp` | 23 | Blocking `connect/publish/subscribe/unsubscribe/disconnect` interface with timeout-aware callback integration |
| `async_client.h/.cpp` | 24 | Non-blocking callback interface with one internal dispatch thread for completions and inbound messages |
| `client_config.h/.cpp` | 25 | Unified client configuration object with defaults, validation, and CONNECT packet builder |

## ClientConfig

`ClientConfig` provides one place for tunable client settings:

- broker target (`broker_host`, `broker_port`, `transport`),
- protocol identity and credentials (`client_id`, username/password),
- CONNECT flags and limits (`clean_start`, `keep_alive_seconds`,
  `session_expiry_interval_seconds`, `receive_maximum`,
  `topic_alias_maximum`),
- reconnect policy (`reconnect_backoff`),
- per-operation timeout defaults (`connect/publish/subscribe/unsubscribe/
  disconnect`).

Runtime helpers:

- `validate_client_config_or_throw(...)` validates required fields and ranges,
- `build_connect_packet(...)` maps `ClientConfig` to MQTT CONNECT model,
- `default_port_for_transport(...)` returns transport-specific default ports.

## SyncClient

`SyncClient` exposes a synchronous API that hides internal state-machine modules
behind operation methods:

- `connect(...)` blocks until negotiation callback returns or throws.
- `connect()` uses configured CONNECT model and configured connect timeout.
- `publish(...)` blocks through QoS completion path:
  - QoS 0: send and return immediately,
  - QoS 1: wait for `PUBACK`,
  - QoS 2: wait for `PUBREC`, send `PUBREL`, wait for `PUBCOMP`.
- `subscribe(...)` blocks until `SUBACK` and applies accepted filters.
- `unsubscribe(...)` blocks until `UNSUBACK` and removes successful filters.
- `disconnect(...)` sends DISCONNECT and disables auto reconnect for the local
  user-initiated close path.

Callback integration model:

- transport/network integration is injected through `SyncClientCallbacks`,
- caller-supplied timeout overloads are available for each blocking operation,
- no-timeout overloads use `ClientConfig` per-operation timeout defaults,
- missing mandatory callback for an operation raises `ClientException`.

State model:

- connection state (`is_connected`) is tracked inside the facade,
- current configuration is exposed through `client_config()`,
- subscription state is managed by `ClientSubscriptionManager`,
- outbound QoS state is managed by `ClientPublishPipeline`,
- reconnect disable-on-user-disconnect uses `ReconnectController`.

## AsyncClient

`AsyncClient` exposes non-blocking variants for connect, publish, subscribe,
unsubscribe, and disconnect:

- `async_connect(...)` enqueues a connect task and reports completion callback,
- `async_connect(...)` has a config-default overload using `ClientConfig`
  CONNECT packet and timeout,
- `async_publish(...)` enqueues a publish task and reports final reason code,
- `async_subscribe(...)` enqueues subscribe and reports SUBACK reason list,
- `async_unsubscribe(...)` enqueues unsubscribe and reports UNSUBACK reasons,
- `async_disconnect(...)` enqueues disconnect without blocking caller.

Callback and threading model:

- all operations are queued and executed by one internal dispatch thread,
- completion callbacks run on this same dispatch thread,
- inbound publish packets are fed via `on_inbound_publish(...)`,
- subscribed message delivery is forwarded to one registered message handler,
- callback errors are propagated as `AsyncOperationError` payloads.

Integration model:

- transport and protocol callbacks are still supplied through
  `SyncClientCallbacks`, reused by wrapped `SyncClient`,
- no-timeout async overloads use `ClientConfig` per-operation defaults,
- asynchronous subscribe requests keep topic filters and options but use the
  global message handler for inbound delivery.
