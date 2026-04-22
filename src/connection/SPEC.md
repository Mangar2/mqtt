# connection — Connection Handler + Connection Manager (Modules 7 + 23 + 24)

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
| `client_handler.h/.cpp` | 24 | `ClientHandler` — thin adapter that forwards accepted sockets into connection-flow orchestration |
| `connect_phase_flow.h/.cpp` | 24 | CONNECT + AUTH handshake phase (`Broker::handle_connect`, `Broker::handle_auth_packet`) |
| `runtime_phase_flow.h/.cpp` | 24 | post-CONNECT runtime packet loop and dispatch |
| `connection_flow_support.h/.cpp` | 24 | shared transport/codec helpers for connect/runtime phases |
| `outbound_queue_bridge.h/.cpp` | 24 | outbound queue bridging helpers (drain pending messages, transfer pending messages between queue instances) |
| `connection_manager.h/.cpp` | 23 | `ConnectionManager` — owns listeners, IoReactor registration, and WorkerPool-dispatched connection jobs |

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

## 24 Lean ClientHandler

`ClientHandler::run(conn, broker, config, is_ws)` is the top-level orchestrator.

Module-24 flow is split into focused components:
- `client_handler` owns full lifecycle orchestration for one socket.
- `connect_phase_flow` owns CONNECT + AUTH progression.
- `runtime_phase_flow` owns post-CONNECT packet processing.
- `connection_flow_support` provides transport + packet utility helpers.

Implemented behavior:
- Optional WebSocket upgrade handshake when `is_ws=true`.
- Socket timeout setup and `WriteQueue` sink binding for direct socket flush.
- Per-connection `WriteQueue` capacity sourced from
	`BrokerConfig::write_queue_max_bytes`.
- CONNECT handshake handling via `Broker::handle_connect()` plus enhanced-auth loop via `Broker::handle_auth_packet()`.
- CONNECT/auth handshake must complete within 30 seconds from socket accept; otherwise the broker closes the transport without entering runtime session handling.
- Construction of `ClientSession` after successful CONNACK and registration of the per-client `OutboundQueue`.
	- Effective keep-alive source: `broker.server_keep_alive` override when non-zero, otherwise CONNECT keep-alive.
- Runtime per-packet dispatch loop with strict delegation:
	- `PUBLISH` → `ClientSession::on_publish()` + `Broker::handle_publish()`
	- `SUBSCRIBE` → `Broker::handle_subscribe()`
	- `UNSUBSCRIBE` → `Broker::handle_unsubscribe()`
	- QoS ACK packets → `ClientSession::on_*()`
	- `PINGREQ` → `PINGRESP`
	- `AUTH` → `ClientSession::on_auth()`
	- `DISCONNECT` → records reason + optional session-expiry override
- Keep-alive timeout handling (`ReasonCode::KeepAliveTimeout`) and broker-running check each loop iteration.
- Teardown via `Broker::handle_disconnect()` on clean close or `Broker::handle_connection_lost()` on abrupt transport loss.

Runtime protocol error handling:
- On protocol violations after CONNECT, `ClientHandler` sends DISCONNECT with
	reason code `ProtocolError` (0x82).
- When CONNECT requested Problem Information, protocol-error DISCONNECT frames
	include a non-empty `ReasonString` property.
- DISCONNECT packets that attempt to increase Session Expiry from 0 to
	non-zero are rejected with protocol-error DISCONNECT.

---

## 23 ConnectionManager

`ConnectionManager` extracts listener and bounded worker lifecycle from `Broker`.

Responsibilities:

- Own MQTT and optional WebSocket `TcpListener` instances.
- Own one `IoReactor` and register listener sockets on startup.
- Accept incoming sockets from reactor callbacks and submit accept jobs to
	`WorkerPool`.
- Emit optional structured trace events for accept anomalies (trace) and client
	job submit failures (warning).
- Stop reactor, listeners, and worker pool on shutdown.
- Request socket shutdown for active slots, wait bounded time for connection
	jobs to drain, then stop worker pool.

Public API:

```cpp
using ClientHandlerCallback =
		std::function<void(std::unique_ptr<TcpConnection>, bool is_ws)>;

ConnectionManager(uint16_t mqtt_port,
									uint16_t ws_port,
									ClientHandlerCallback callback,
									std::chrono::milliseconds client_join_timeout =
											std::chrono::seconds(2),
									StructuredTracer *structured_tracer = nullptr);

void start();
void stop() noexcept;
[[nodiscard]] bool is_running() const noexcept;
```
