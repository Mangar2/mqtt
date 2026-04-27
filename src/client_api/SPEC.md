# client_api — Synchronous client interface (Step 23)

Blocking public client facade that wraps lower-level client components.
Depends on `client/` and `data_model/`.

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `sync_client.h/.cpp` | 23 | Blocking `connect/publish/subscribe/unsubscribe/disconnect` interface with timeout-aware callback integration |

## SyncClient

`SyncClient` exposes a synchronous API that hides internal state-machine modules
behind operation methods:

- `connect(...)` blocks until negotiation callback returns or throws.
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
- each blocking operation forwards the caller-provided timeout in milliseconds
  to wait callbacks,
- missing mandatory callback for an operation raises `ClientException`.

State model:

- connection state (`is_connected`) is tracked inside the facade,
- subscription state is managed by `ClientSubscriptionManager`,
- outbound QoS state is managed by `ClientPublishPipeline`,
- reconnect disable-on-user-disconnect uses `ReconnectController`.
