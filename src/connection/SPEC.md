# connection — Connection Handler + Connection Manager (Modules 7 + 17 + 23)

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
| `client_handler.h/.cpp` | 17 | `ClientHandler` — temporary placeholder that closes accepted connections immediately |
| `connection_manager.h/.cpp` | 23 | `ConnectionManager` — owns listeners, accept loops, and tracked client threads |

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

`ClientHandler::run(conn, broker, config, is_ws)` is currently a temporary stub.

Current behavior:
- Accept ownership of one TCP connection.
- Ignore protocol processing for now.
- Close the connection immediately and return.

Rationale:
- The previous Module 17 implementation is intentionally removed.
- A clean placeholder keeps the build stable while a redesigned handler is implemented later.

Constraints for current placeholder:
- No MQTT packet parsing.
- No authentication or session handling.
- No broker state changes through `register_connection()` / `unregister_connection()`.
- No QoS, subscribe, unsubscribe, ping, will, or disconnect processing.

---

## 23 ConnectionManager

`ConnectionManager` extracts listener and thread lifecycle from `Broker`.

Responsibilities:

- Own MQTT and optional WebSocket `TcpListener` instances.
- Start one accept-loop thread per enabled listener.
- Spawn tracked client threads and clean up finished threads incrementally.
- Stop listeners and accept loops on shutdown.
- Wait for client threads up to a configured timeout and request socket shutdown
	for remaining clients to unblock pending reads.

Public API:

```cpp
using ClientHandlerCallback =
		std::function<void(std::unique_ptr<TcpConnection>, bool is_ws)>;

ConnectionManager(uint16_t mqtt_port,
									uint16_t ws_port,
									ClientHandlerCallback callback,
									std::chrono::milliseconds client_join_timeout =
											std::chrono::seconds(2));

void start();
void stop() noexcept;
[[nodiscard]] bool is_running() const noexcept;
```
