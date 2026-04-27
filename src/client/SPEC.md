# client — Client-side MQTT components (Steps 16, 17, 18, 19, 20)

Reusable client-only building blocks for outbound broker connections.
Depends on `data_model/`, `codec/`, and `network/`.

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `client_error.h` | 16-20 | `ClientError` enum and `ClientException` |
| `keep_alive_manager.h/.cpp` | 16 | Active keep-alive poller (`PINGREQ` scheduling + `PINGRESP` timeout detection) |
| `outbound_topic_alias_manager.h/.cpp` | 17 | Outbound topic-alias assignment/reuse for PUBLISH packets |
| `connection_negotiator.h/.cpp` | 18 | Outbound TCP dial + CONNECT/CONNACK handshake negotiation |
| `session_state_keeper.h/.cpp` | 19 | Client-side session-state keeper (subscriptions, session expiry, outbound inflight replay snapshot) |
| `subscription_manager.h/.cpp` | 20 | Client-side SUBSCRIBE/UNSUBSCRIBE manager with ACK correlation and inbound callback dispatch |

## KeepAliveManager (Step 16)

`KeepAliveManager` drives client-side keep-alive behavior:

- `poll(now)` emits:
  - `SendPingreq` when idle interval is exceeded,
  - `Timeout` when `PINGRESP` deadline is exceeded,
  - `None` otherwise.
- `note_activity()` resets idle state after any inbound/outbound traffic.
- `on_pingresp()` clears pending ping and refreshes activity timestamp.
- `encode_pingreq_frame()` builds a wire-ready MQTT `PINGREQ` frame.

Keep-alive value `0` disables the manager.

## OutboundTopicAliasManager (Step 17)

`OutboundTopicAliasManager` rewrites outbound `PublishPacket` values:

- First publish for a topic: assigns an alias and keeps topic string.
- Repeated publish for same topic: reuses alias and clears topic string.
- Active alias count never exceeds configured `max_aliases`.
- Existing topic-alias property is updated in place.

`reset()` clears all mappings.

## ConnectionNegotiator (Step 18)

`ConnectionNegotiator` provides client-side connect negotiation helpers:

- `dial_tcp(host, port)` resolves DNS and opens a TCP connection.
- `negotiate(connection, connect_packet, read_timeout_ms)`:
  - sends `CONNECT`,
  - waits for first response packet,
  - requires `CONNACK`,
  - throws `NegotiationRejected` on error reason codes,
  - returns parsed negotiated values (`session_present`, `receive_maximum`,
    `topic_alias_maximum`, optional `server_keep_alive`, optional
    `assigned_client_id`, full `connack_properties`).

## ClientSessionStateKeeper (Step 19)

`ClientSessionStateKeeper` stores reconnect-relevant client session data:

- tracks active subscriptions with upsert/remove semantics,
- stores session expiry interval,
- stores/captures outbound inflight entries,
- builds reconnect restore plan depending on `clean_start`.

State handling API:

- `upsert_subscription(...)`, `remove_subscription(...)`, `clear_subscriptions()`
- `set_outbound_inflight(...)`, `capture_outbound_inflight(...)`
- `build_restore_plan(clean_start)`
- `snapshot()` and `apply_snapshot(...)`

Behavior guarantees:

- only outbound inflight entries are retained,
- packet_id `0` entries are ignored,
- outbound inflight replay list is kept in deterministic order
  (timestamp, then packet_id),
- snapshot client ID mismatch is rejected.

## ClientSubscriptionManager (Step 20)

`ClientSubscriptionManager` orchestrates client-side subscription lifecycle:

- `begin_subscribe(...)` creates a SUBSCRIBE packet with packet-id tracking and
  stores pending callback bindings.
- `on_suback(...)` matches SUBACK by packet-id, activates accepted
  subscriptions, and keeps rejected filters inactive.
- `begin_unsubscribe(...)` creates an UNSUBSCRIBE packet with packet-id
  tracking.
- `on_unsuback(...)` matches UNSUBACK by packet-id and removes successful
  filters from local active state.
- `dispatch_inbound_publish(...)` validates the topic name, matches active
  filters with topic wildcard rules, and invokes matching callbacks.

Behavior guarantees:

- unknown packet-id ACKs are rejected with `ProtocolError`,
- topic filter/topic name validation failures are mapped to `InvalidPacket`,
- callbacks are stored per topic filter and replaced on re-subscribe,
- `clear()` removes active and pending state.
