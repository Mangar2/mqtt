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
| `connection_session.h/.cpp` | step 05 | `ConnectionSession` — heap-owned per-connection state used by serialized per-fd worker jobs |
| `client_handler.h/.cpp` | 24 | stateless `mqtt::client_handler::process_*_job` handlers (`Accept`, `Decode`, `Drain`, `Close`) |
| `connection_flow_support.h/.cpp` | 24 | shared encode/decode/property helpers used by step processors |
| `outbound_queue_bridge.h/.cpp` | 24 | outbound queue bridging helpers (drain pending messages, transfer pending messages between queue instances) |
| `connection_manager.h/.cpp` | 23 | `ConnectionManager` — owns listeners, IoReactor registration, and WorkerPool-dispatched connection jobs |
| `decode_step.h/.cpp` | step 05 | single-packet decode step driven by `StreamBuffer` and phase dispatch |
| `handshake_step.h/.cpp` | step 05 | single-packet handshake step for CONNECT-first processing |
| `runtime_step.h/.cpp` | step 05 | single-packet runtime dispatch step |
| `outbound_drain_step.h/.cpp` | step 05 | drains `ClientSession` outbound frames into session pending-write storage |
| `close_step.h/.cpp` | step 05 | close-finalization helper calling broker disconnect/lost paths |

## 7.1 ConnectionStateMachine

## Threading Model Note (step 05)

- `ConnectionSession` is intentionally not internally synchronized.
- The executor guarantees per-fd serialization: for one connection/socket fd,
  only one worker job executes at a time.
- Different fds may be processed in parallel by different worker threads.

## Step Helpers (step 05)

- `decode_step` decodes at most one packet from `ConnectionSession::stream_buffer()`.
- `handshake_step` handles handshake packet semantics one packet at a time.
- `runtime_step` handles runtime packet semantics one packet at a time.
- `outbound_drain_step` drains outbound session frames without transport writes.
- `close_step` runs broker close bookkeeping without touching reactor state.

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

## 24 Stateless job processors

Connection handling is now split into stateless worker-job handlers:
- `process_accept_job(...)`: adopts accepted socket, creates `ConnectionSession`, inserts `ConnectionTable::Entry`, registers reactor callbacks.
- `process_decode_job(...)`: performs non-blocking reads, decodes up to a bounded packet budget, dispatches via step helpers.
- `process_drain_job(...)`: drains queued outbound frames into slot write-buffer and drives non-blocking write progress.
- `process_close_job(...)`: finalizes broker bookkeeping, unregisters reactor fd, closes socket, removes table entry.

Step helpers (`decode_step`, `handshake_step`, `runtime_step`, `outbound_drain_step`, `close_step`) process one bounded unit of work each; no blocking runtime while-loops remain in this module.

---

## 23 ConnectionManager

`ConnectionManager` extracts listener/reactor/worker lifecycle from `Broker`.

Responsibilities:

- Own MQTT and optional WebSocket `TcpListener` instances.
- Own one `IoReactor` and register listener sockets on startup.
- Accept incoming sockets from reactor callbacks and submit accept jobs to `WorkerPool`.
- Dispatch worker jobs to `client_handler::process_*_job` functions.
- Stop order: reactor → listeners → socket shutdown snapshot → worker pool → table clear.

Public API:

```cpp
ConnectionManager(uint16_t mqtt_port,
									uint16_t ws_port,
									Broker &broker,
									const BrokerConfig &config,
									std::size_t worker_min_threads = 2U,
									std::size_t worker_max_threads = 0U,
									StructuredTracer *structured_tracer = nullptr);

void start();
void stop() noexcept;
[[nodiscard]] bool is_running() const noexcept;
```
