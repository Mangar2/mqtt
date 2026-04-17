# connection — Connection Handler (Modules 7 + 17)

Manages the lifecycle of a single MQTT 5.0 client connection.
Depends on: data_model (1), codec (2), qos (5), network (6), auth (8), session_manager (9), message_router (10), will_manager (11), transport (12), broker (16).

## Files

| File | Plan ref | Contents |
|------|----------|---------|
| `connection_error.h` | 7 | `ConnectionError` enum and `ConnectionException` |
| `connection_state.h/.cpp` | 7.1 | `ConnectionStateMachine` — lifecycle states Connecting → Connected → Disconnecting → Closed |
| `keep_alive_timer.h/.cpp` | 7.2 | `KeepAliveTimer` — deadline tracking at 1.5 × Keep Alive interval |
| `topic_alias_table.h/.cpp` | 7.3 | `TopicAliasTable` — inbound and outbound alias↔topic mappings with maximum enforcement |
| `receive_maximum.h/.cpp` | 7.4 | `ReceiveMaximum` — inflight QoS 1+2 packet counter with pause/resume |
| `client_handler.h/.cpp` | 17 | `ClientHandler` — full MQTT 5.0 per-client session loop on a dedicated thread |

## 7.1 ConnectionStateMachine

States: `Connecting` → `Connected` → `Disconnecting` → `Closed`

- `on_connect()` — call on CONNECT received. Connecting → Connected. Throws DuplicateConnect if already Connected. Throws InvalidState if Disconnecting or Closed.
- `on_disconnect()` — call on DISCONNECT received. Connected → Disconnecting. Throws InvalidState otherwise.
- `on_connection_lost()` — call on TCP error/EOF. Any state → Closed.
- `close()` — force close. Any state → Closed.
- `enforce_not_connecting()` — throws ConnectRequired if still Connecting (enforces CONNECT as first packet).
- `state()` — returns current `ConnectionState`.

## 7.2 KeepAliveTimer

- Constructed with `keep_alive_seconds` (0 = disabled).
- `reset()` — sets deadline to now + 1.5 × keep_alive_seconds.
- `is_expired()` — true if deadline has passed and timer is enabled.
- `is_enabled()` — false if keep_alive == 0.

## 7.3 TopicAliasTable

- Inbound (client → broker): alias (uint16_t) → topic (string).
- Outbound (broker → client): topic (string) → alias (uint16_t).
- `set_inbound(alias, topic)` — stores mapping; throws if alias > max or alias == 0.
- `get_inbound(alias)` — returns topic; throws AliasNotFound if missing.
- `set_outbound(topic, alias)` — stores mapping; throws if alias > max or alias == 0.
- `get_outbound(topic)` — returns optional alias.
- `reset()` — clears all mappings.

## 7.4 ReceiveMaximum

- Constructed with `max` (uint16_t, default 65535).
- `acquire()` — increments inflight count; returns false if at maximum (pause signal).
- `release()` — decrements count; throws InvalidState if already 0.
- `is_paused()` — true if inflight == max.
- `available()` — remaining capacity.

---

## 17 ClientHandler

`ClientHandler::run(conn, broker, config, is_ws)` executes the complete MQTT 5.0 connection lifecycle for one accepted TCP connection. It is called on a dedicated `std::thread` (spawned by `Broker::open_listeners()`) and blocks until the connection closes. A single `ClientHandler` instance must not be used concurrently.

### 17.1 Transport setup

**17.1.1 Plain TCP** — no preamble; proceed directly to 17.2.

**17.1.2 WebSocket upgrade** — when `is_ws == true`:
- Feed raw bytes into `WebSocketHandshake` until `is_complete()` returns `true`.
- Call `build_response()` and write the HTTP 101 response back.
- All subsequent reads/writes are wrapped in `WebSocketFrameCodec` binary frames.
- Close the connection (return silently) on any handshake error.

**17.1.3 Receive timeout** — `TcpConnection::set_receive_timeout(500 ms)` is set immediately after the optional WebSocket upgrade. This makes `read()` return with `last_read_timed_out() == true` every 500 ms so the keep-alive deadline can be polled without blocking indefinitely.

### 17.2 CONNECT handshake

**17.2.1 First-packet enforcement** — up to 20 read attempts (each up to 500 ms) are made to receive the first complete MQTT packet. If the packet is not a `ConnectPacket`, a `DISCONNECT(ProtocolError)` is sent and the connection is closed. If no packet arrives in time, the connection is closed silently.

**17.2.2 Authentication** — credentials from the `ConnectPacket` are passed to `EnhancedAuthHandler::initiate()`.
- `AuthStatus::Failure` → send `CONNACK` with the returned reason code and close.
- `AuthStatus::Continue` → exchange `AUTH(ContinueAuthentication)` ↔ `AUTH` packets until `Success` or `Failure`. Failure sends `CONNACK(bad reason_code)` and closes.
- `AuthStatus::Success` → continue to 17.2.3.

**17.2.3 Session open/resume** — `SessionManager::handle_connect(connect, close_fn)` is called. On exception a `CONNACK(ClientIdentifierNotValid)` is sent and the connection is closed.

**17.2.4 Will storage** — if `ConnectPacket::will` is present, `WillPublisher::on_connect(client_id, will_msg)` stores the will, including `WillDelayInterval` if present.

**17.2.5 CONNACK** — sent with `session_present` from `SessionOpenResult`. Server properties advertised if non-default:
- `ReceiveMaximum` — when `config.receive_maximum != 65535`.
- `TopicAliasMaximum` — when `config.topic_alias_maximum != 0`.

### 17.3 Per-packet dispatch loop

Runs until `!running || !broker.is_running()`.

On each iteration:
1. Read a chunk (up to 4 096 bytes) from the socket.
2. On EOF: exit the loop.
3. On data: feed into `StreamBuffer` (or `WebSocketFrameCodec` → `StreamBuffer` for WS).
4. **17.3.2 Keep-alive reset** — `KeepAliveTimer::reset()` on every non-timeout read.
5. **17.5.1 Keep-alive check** — if `KeepAliveTimer::is_expired()`, send `DISCONNECT(KeepAliveTimeout)` and exit.
6. Dispatch each complete packet via `std::visit`:

| Packet type | Action |
|-------------|--------|
| `PublishPacket` QoS 0 | Route via `Broker::route_message()` |
| `PublishPacket` QoS 1 | `Qos1StateMachine::on_publish_received()` → send `PUBACK`; route if not dup |
| `PublishPacket` QoS 2 | `Qos2StateMachine::on_publish_received()` → send `PUBREC`; route if not duplicate |
| `PubackPacket` | `Qos1StateMachine::on_puback_received()`; `ReceiveMaximum::release()` |
| `PubrecPacket` | `Qos2StateMachine::on_pubrec_received()` → send `PUBREL` |
| `PubrelPacket` | `Qos2StateMachine::on_pubrel_received()` → send `PUBCOMP` |
| `PubcompPacket` | `Qos2StateMachine::on_pubcomp_received()`; `ReceiveMaximum::release()` |
| `SubscribePacket` | Store each subscription via `SubscriptionStore::store()`; deliver matching retained messages; send `SUBACK` |
| `UnsubscribePacket` | `SubscriptionStore::remove()` per filter; send `UNSUBACK` |
| `PingreqPacket` | Send `PINGRESP` |
| `DisconnectPacket` | Record `clean_disconnect = true`, reason code, optional session-expiry override; exit loop |
| `AuthPacket` | Re-authentication via `EnhancedAuthHandler::reauthenticate()`; send `AUTH(Success/Continue)` or `DISCONNECT(NotAuthorized)` |
| Malformed packet | Send `DISCONNECT(MalformedPacket)`; exit loop |

### 17.4 Outbound message delivery

**17.4.1 Send callback** — `send_message` lambda is registered with `Broker::register_connection(client_id, send_message)`. It encodes and enqueues outbound messages:
- QoS 0: encode `PUBLISH` and enqueue directly.
- QoS 1: `ReceiveMaximum::acquire()` (drop if paused); `Qos1StateMachine::initiate_publish()` → enqueue `PUBLISH`.
- QoS 2: `ReceiveMaximum::acquire()` (drop if paused); `Qos2StateMachine::initiate_publish()` → enqueue `PUBLISH`.

**17.4.2 Topic alias resolution** — inbound topic aliases are resolved via `TopicAliasTable::get_inbound()` inside `Broker::route_message()`.

**17.4.3 Receive Maximum** — constructed from `ReceiveMaximum` property in the CONNECT packet (default 65535). Limits outbound inflight QoS 1+2 packets.

**17.4.4 Drain thread** — a `std::jthread` runs `WriteQueue::run_drain(*conn)` for the entire session. Automatically joins on teardown when `WriteQueue::stop()` is called.

**17.5.3 Offline queue flush** — for resumed sessions (`session_present == true`), `MessageRouter::flush_offline_queue(client_id)` is called immediately after registering the send callback.

### 17.5 Teardown

1. `WriteQueue::stop()` — signals the drain thread to finish; `jthread` destructor joins it.
2. **Will handling**:
   - Clean disconnect (`DisconnectPacket` received): `WillPublisher::on_disconnect(client_id, reason_code, now)`.
   - Unclean close (EOF, keep-alive timeout, protocol error): `WillPublisher::on_connection_lost(client_id, now)`.
3. `Broker::unregister_connection(client_id)` — removes the send callback.
4. `SessionManager::handle_disconnect(client_id, expiry_override, now)` — persists or expires the session.

### 17.6 Constants (internal)

| Constant | Value | Purpose |
|----------|-------|---------|
| `k_recv_timeout_ms` | 500 ms | Socket receive timeout for keep-alive polling |
| `k_read_chunk` | 4 096 bytes | Read buffer size per iteration |
| First-packet max tries | 20 × 500 ms = 10 s | Hard limit before closing unauthenticated connections |
