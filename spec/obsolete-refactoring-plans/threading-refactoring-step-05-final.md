# Step 05 (Final) — Replace Per-Connection Threads with Reactor + Worker-Job Model

Reference: `threading-refactoring.md` §3, §4, §5.4, §6, §7, §10, §15.
Supersedes: `threading-refactoring-step-05-worker-integration.md` (earlier attempts only created skeletons; the per-connection blocking loop was never replaced).

> **Read this in full before you write a single line of code.**
> The failure mode of every previous attempt: people built helper classes but
> kept `client_handler::run()` blocking inside `WorkerPool`. That changes
> nothing — the worker pool then *is* the per-connection thread pool. This
> document tells you exactly which blocking constructs to remove and exactly
> what to replace them with.

---

## 1. What is wrong today (verified, do not re-investigate)

1. `ConnectionManager::handle_connection_job()` only handles `JobType::Accept`
   and immediately calls `callback_(...)` which is wired to
   `ClientHandler::run()` in `Broker::handle_client_connection`.
2. `ClientHandler::run()` runs the **entire connection lifetime** synchronously:
   - `establish_connect_session()` blocks on `read_transport_chunk()`
     (handshake, until CONNACK done).
   - `run_connected_session_loop()` blocks in `while (broker.is_running())`
     calling `read_transport_chunk()` until disconnect.
3. Therefore each accepted connection occupies one `WorkerPool` thread for its
   complete lifetime. With `worker_max_threads ≈ hardware_concurrency * 4`
   this caps **simultaneous connections at ~32 on a typical Pi**, not 3200.
4. `IoReactor` is only used for listener accept events; it never observes
   client read or write readiness. `ConnectionTable::add(ConnectionSlot{fd})`
   is called but the slot's read/write buffers are never used.
5. `WriteQueue` still owns its drain logic and the `set_sink()` callback writes
   directly to the socket from whichever thread enqueues.
6. `runtime_phase_flow.cpp` and `connect_phase_flow.cpp` are written as one
   long synchronous loop that owns local stack state
   (`StreamBuffer`, `WriteQueue`, `ClientSession`, `OutboundQueue`,
   `TopicAliasTable`, …).
7. Mutexes have been **removed from facades**. All shared singletons
   (`SubscriptionStore`, `SessionStore`, `MessageRouter`, `WillPublisher`, …)
   carry their own internal locks per `thread-safety-inventory.md`.
   **Do NOT add facade mutexes back** — that was a previously documented
   mistake.

---

## 2. End-state we must reach

```
Listener fd  ─► IoReactor (1 thread)
                    │  accept event
                    ▼
              JobScheduler.submit(AcceptJob{fd, is_ws})
                    │
                    ▼
              WorkerPool (N elastic threads)
                    │  process_accept_job
                    ▼
              creates ConnectionSession(fd) on heap,
              registers slot in ConnectionTable,
              registers fd with IoReactor for read events
                    │
                    │  client bytes arrive
                    ▼
              IoReactor read-ready callback
                  ↳ nb_read into ConnectionSlot.read_buffer
                  ↳ JobScheduler.submit(DecodeJob{fd})
                    │
                    ▼
              WorkerPool worker
                  ↳ process_decode_job(slot, session, broker)
                       • try_decode_packet from slot.read_buffer
                       • handle packet (calls broker facade, updates session)
                       • encode response into slot.write_buffer
                       • drain session.outbound_queue → slot.write_buffer
                       • if write_buffer non-empty → reactor.arm_write(fd)
                       • mark_done(fd) → next queued job for this fd, if any

Outbound publish (broker → client):
  Broker facade pushes Message to ConnectionSession.outbound_queue
  ↳ JobScheduler.submit(DrainJob{fd})
  ↳ Worker encodes pending messages into slot.write_buffer
  ↳ reactor.arm_write(fd)
  ↳ IoReactor write-ready callback: nb_write from slot.write_buffer
       • on WouldBlock → keep write armed
       • on fully drained → reactor.disarm_write(fd)

Close (peer EOF, error, kicked, takeover):
  IoReactor read-ready returns Closed/Error
  ↳ JobScheduler.submit(CloseJob{fd})
  ↳ Worker process_close_job:
       • Broker.handle_connection_lost / handle_disconnect
       • broker.unregister_connection
       • reactor.unregister(fd)
       • close socket
       • ConnectionTable.remove(fd)  ← destroys ConnectionSession
```

Total threads at runtime: `1 (main) + 1 (reactor) + 1 (pool scaling) + N workers`.
**No thread is ever created per connection.**

---

## 3. Concrete file-by-file work list

Do the steps in the listed order. After each numbered group: build, run unit
tests, fix regressions, then continue.

### 3.1  NEW — `src/connection/connection_session.h/.cpp`

Heap-allocated per-connection state. Replaces every stack local in
`run_client_handler_flow`. **No mutex** — access is serialized via
`JobScheduler` (at most one job per fd at a time).

```cpp
class ConnectionSession {
public:
  ConnectionSession(std::unique_ptr<TcpConnection> connection,
                    std::unique_ptr<WebSocketTransport> ws_transport,
                    bool is_websocket,
                    const BrokerConfig &config);

  // Owned per-connection objects:
  TcpConnection &connection() noexcept;
  WebSocketTransport *ws_transport() noexcept;        // may be nullptr
  bool is_websocket() const noexcept;
  StreamBuffer &stream_buffer() noexcept;
  TopicAliasTable &topic_alias_table() noexcept;
  ReceiveMaximum &inbound_receive_window() noexcept;

  // Set after CONNECT completes:
  void install_client_session(std::unique_ptr<ClientSession>);
  ClientSession *client_session() noexcept;
  std::shared_ptr<OutboundQueue> outbound_queue() noexcept;

  // Lifecycle phase mirroring ConnectionSlot::phase
  enum class Phase { Handshake, Connected, Closing };
  Phase phase() const noexcept;
  void set_phase(Phase) noexcept;

  // Disconnect bookkeeping populated by workers:
  RuntimeDisconnectState &disconnect_state() noexcept;

private:
  // owned objects above + std::unique_ptr<ClientSession>, etc.
};
```

`ConnectionSlot` already exists in `src/network`. Either:
- store `std::unique_ptr<ConnectionSession>` **next to** the slot inside
  `ConnectionTable` (preferred — keeps `ConnectionSlot` purely transport),
  by changing the table to store `struct Entry { ConnectionSlot slot;
  std::unique_ptr<ConnectionSession> session; }`; **or**
- give `ConnectionSlot` a `unique_ptr<ConnectionSession>` member.

Pick option 1. Update `ConnectionTable` API:
- `bool add(int fd, ConnectionSlot slot, std::unique_ptr<ConnectionSession>)`
- `Entry *find(int fd)`
- existing `remove(fd)` keeps working.

### 3.2  NEW — split helpers under `src/connection/`

Replace `connect_phase_flow.cpp` and `runtime_phase_flow.cpp` (which contain
blocking `while` loops) with **non-blocking, single-step** helpers:

| New file | Function | Returns |
|----------|----------|---------|
| `decode_step.cpp/.h` | `DecodeOutcome decode_one_packet(ConnectionSession&, Broker&)` | enum: `NeedMore`, `Processed`, `ProtocolError`, `Disconnected` |
| `handshake_step.cpp/.h` | `HandshakeOutcome process_handshake_packet(ConnectionSession&, Broker&, AnyPacket)` | enum: `Continuing`, `ConnectAccepted`, `Rejected` |
| `runtime_step.cpp/.h` | `RuntimeOutcome process_runtime_packet(ConnectionSession&, Broker&, AnyPacket)` | enum: `Continuing`, `DisconnectClean`, `DisconnectError` |
| `outbound_drain_step.cpp/.h` | `void drain_outbound_to_write_buffer(ConnectionSession&, Broker&)` | drains `client_session->drain_outbound()` and `outbound_queue` into `slot.write_buffer` (encoded frames) |
| `close_step.cpp/.h` | `void finalize_close(ConnectionSession&, Broker&)` | calls `Broker::handle_disconnect` or `handle_connection_lost`, unregisters connection |

Reuse the **existing per-packet dispatcher logic** in
`runtime_phase_flow.cpp` — extract its inner switch/visitor into the new
`runtime_step.cpp`, but **delete the surrounding `while` loops**. The reactor +
job scheduler now drive iteration.

Delete files when the new helpers cover their content:
- `connection/connect_phase_flow.cpp/.h`
- `connection/runtime_phase_flow.cpp/.h`

### 3.3  REWRITE — `src/connection/client_handler.h/.cpp`

Replace today's `class ClientHandler { void run(...); }` with **stateless free
functions** in namespace `mqtt::client_handler`:

```cpp
namespace mqtt::client_handler {

void process_accept_job(const AcceptJobPayload &payload,
                        ConnectionTable &table,
                        IoReactor &reactor,
                        JobScheduler &scheduler,
                        Broker &broker,
                        const BrokerConfig &config);

void process_decode_job(int fd,
                        ConnectionTable &table,
                        IoReactor &reactor,
                        JobScheduler &scheduler,
                        Broker &broker);

void process_drain_job(int fd,
                       ConnectionTable &table,
                       IoReactor &reactor,
                       Broker &broker);

void process_close_job(int fd,
                       ConnectionTable &table,
                       IoReactor &reactor,
                       Broker &broker);

} // namespace mqtt::client_handler
```

Mandatory rules for these functions:
- **No blocking I/O.** No call to `TcpConnection::read*`, no
  `read_transport_chunk`, no `WriteQueue::drain`.
- **No new threads.** Never `std::thread`, never `std::async`.
- **Hold no mutex.** Per-connection serialization is the JobScheduler's job.
- Must be safe to call from any worker thread, but never concurrently for the
  same `fd` (scheduler guarantees this).
- On read returning `Closed/Error` or any disconnect path → submit a
  `CloseJob` via `JobScheduler` and return; **do not** finalize close inline,
  to keep the per-job work bounded.

`process_decode_job` outline:
```
entry = table.find(fd);  if (!entry) return;
session = *entry->session;
loop:
  outcome = decode_one_packet(session, broker);
  switch (outcome):
    NeedMore        -> break
    Processed       -> continue (bounded by max packets per job, e.g. 32)
    ProtocolError   -> session.disconnect_state.error = true; submit Close; break
    Disconnected    -> submit Close; break
drain_outbound_to_write_buffer(session, broker);
if (slot.write_size() > 0) reactor.arm_write(fd);
```

`process_drain_job` outline:
```
entry = table.find(fd);  if (!entry) return;
drain_outbound_to_write_buffer(*entry->session, broker);
if (entry->slot.write_size() > 0) reactor.arm_write(fd);
```

`process_accept_job` outline:
```
fd = payload.socket_handle;
set_nonblocking(fd);
auto tcp = TcpConnection::adopt(fd);
auto ws = payload.websocket_connection
            ? std::make_unique<WebSocketTransport>(*tcp) : nullptr;
auto session = make_unique<ConnectionSession>(move(tcp), move(ws),
                                              payload.websocket_connection,
                                              config);
table.add(fd, ConnectionSlot{fd}, move(session));
reactor.register_connection(fd,
    /*read*/ [&scheduler, fd](int) {
       scheduler.submit({JobType::Decode, fd, DecodeJobPayload{}});
    },
    /*write*/ [&scheduler, fd](int) {
       scheduler.submit({JobType::Drain, fd, DrainJobPayload{}});
    });
// Trigger an initial decode in case bytes arrived already (level-triggered safety):
scheduler.submit({JobType::Decode, fd, DecodeJobPayload{}});
```

`process_close_job` outline:
```
entry = table.find(fd);  if (!entry) return;
finalize_close(*entry->session, broker);   // calls handle_disconnect / handle_connection_lost
reactor.unregister(fd);
nb_close(fd);
(void)table.remove(fd);
```

The reactor read callback should also drive a `nb_read` into
`slot.read_buffer` — choose one of:
1. Reactor-side: read in the read callback before submitting the DecodeJob.
2. Worker-side: read at the top of `process_decode_job`.

**Choose option 2.** Keeps the reactor thread minimal (kqueue/epoll dispatch
only) and lets the worker apply the per-connection serialization guarantee
to the read syscall as well.

### 3.4  CHANGE — `src/network/io_reactor.*`

Already exposes `register_connection(fd, read_cb, write_cb)`,
`arm_write(fd)`, `disarm_write(fd)`, `unregister(fd)`. Audit both backends
(`io_reactor_kqueue.cpp`, `io_reactor_epoll.cpp`) and confirm:
- read events are level-triggered or that EAGAIN handling is correct;
- after `arm_write`, the next writable-edge fires the write callback;
- after a fully drained write, callers can call `disarm_write` without
  destroying the entry.

Add a small helper if not present:
```cpp
void IoReactor::wake();   // self-pipe / EVFILT_USER, used to break the wait
                          // when shutting down or when registrations need
                          // immediate effect.
```
Many existing implementations already have an internal wake. If not, add
one — it is required for clean shutdown without using a poll timeout.

### 3.5  CHANGE — `src/network/write_queue.h/.cpp`

Per `threading-refactoring.md` §12 Phase D and step-05 §22 acceptance #8:
- **Delete** `WriteQueue::drain(TcpConnection&)`.
- **Delete** the sink-writer concept. The new pipeline writes only via the
  reactor: workers push bytes into `ConnectionSlot.write_buffer`, the reactor
  performs `nb_write`.
- After deletion, `WriteQueue` is dead code if no consumer remains. **Delete
  the file pair** if and only if grep shows zero remaining users in
  `src/`. Otherwise, slim it to a pure FIFO of `std::vector<uint8_t>` with no
  thread/sink machinery.

Update `src/network/test/TEST_SPEC.md` and corresponding tests.

### 3.6  SHRINK — `src/connection/connection_manager.h/.cpp`

Final shape (target: `≤ 100 lines .h`, `≤ 80 lines .cpp` per §15.4):

```cpp
class ConnectionManager {
public:
  ConnectionManager(uint16_t mqtt_port, uint16_t ws_port,
                    Broker &broker, const BrokerConfig &config,
                    std::size_t worker_min_threads,
                    std::size_t worker_max_threads,
                    StructuredTracer *tracer);

  void start();
  void stop() noexcept;
  bool is_running() const noexcept;

private:
  void on_listener_ready(SocketHandle, bool is_ws);
  void on_worker_job(const ConnectionJob &);

  uint16_t mqtt_port_, ws_port_;
  Broker &broker_;
  const BrokerConfig &config_;
  std::size_t worker_min_, worker_max_;
  StructuredTracer *tracer_;

  std::atomic<bool> running_{false};
  std::optional<TcpListener> mqtt_listener_, ws_listener_;
  ConnectionTable table_;
  std::unique_ptr<IoReactor> reactor_;
  std::unique_ptr<JobScheduler> scheduler_;
  std::unique_ptr<WorkerPool> pool_;
};
```

`on_worker_job` switches on `JobType` and dispatches to the corresponding
`client_handler::process_*_job` free function, then calls
`scheduler_->mark_done(job.connection_fd)` and submits any returned next
job back into `pool_`.

`stop()` order:
1. `running_ = false`
2. `reactor_->stop()`  (so no new jobs)
3. `mqtt_listener_->close(); ws_listener_->close()`
4. shutdown sockets of all entries in `table_` (snapshot fds, `nb_close`)
5. `pool_->stop()`  (joins worker threads — the **only** join site)
6. `table_.clear()` (destroys remaining `ConnectionSession`s)

Remove fully:
- `ClientHandlerCallback callback_;`  (broker is referenced directly now)
- `worker_stop_timeout_` busy-wait — `pool_->stop()` joins deterministically.
- `lifecycle_mutex_`  (start/stop are called once from main).
- `set_socket_blocking*` test helpers.
- The two-constructor overload — keep one constructor.

### 3.7  CHANGE — `src/broker/broker.cpp` and facades

Outbound publish path today: facade calls
`outbound_queue->push(msg)` and `client_handler::run` later notices via
`client_session.drain_outbound()`. New path:

1. `Broker::register_connection(client_id, outbound_queue, fd)` — extend the
   signature with `int fd` (the connection's socket fd). Store the mapping
   `client_id -> fd` in `ActiveConnectionRegistry` (it already owns the
   client_id index; add an `int fd` field to its entry struct).
2. After any facade pushes into an `OutboundQueue`, it must additionally call
   `Broker::wake_outbound(client_id)`. Implementation:
   ```cpp
   void Broker::wake_outbound(std::string_view client_id) {
     auto fd = active_connection_registry_.fd_for(client_id);
     if (fd) job_scheduler_->submit({JobType::Drain, *fd, DrainJobPayload{}});
   }
   ```
3. Pass `JobScheduler*` from `ConnectionManager` into `Broker` at construction
   (or set it after `start()` via a setter — `Broker::set_job_scheduler`).
4. The old `ClientSession::drain_outbound()` still works; we now call it
   inside `outbound_drain_step.cpp` from a worker.

`Broker::handle_disconnect` and `handle_connection_lost` keep their current
signatures — `process_close_job` calls them directly.

### 3.8  Wire main.cpp

`Broker::handle_client_connection(...)` and the
`ClientHandlerCallback` plumbing in `main.cpp` go away. `ConnectionManager`
takes `Broker&` directly and dispatches jobs to `client_handler::process_*`.

---

## 4. Concrete deletions (these MUST be gone afterwards)

Grep proofs an agent must run and report empty for:

```
git grep -nE 'std::thread\b' src/connection src/broker
   →  only allowed hits: WorkerPool internals (src/executor/worker_pool.cpp)
                         and IoReactor backends (src/network/io_reactor_*.cpp)

git grep -n 'WriteQueue::drain'           # zero hits
git grep -n 'WriteQueue::run_drain'       # zero hits
git grep -n 'set_sink'                    # zero hits in src/
git grep -n 'read_transport_chunk'        # zero hits (function deleted)
git grep -n 'run_connected_session_loop'  # zero hits (function deleted)
git grep -n 'establish_connect_session'   # zero hits (function deleted)
git grep -n 'client_threads_'             # zero hits
git grep -n 'cleanup_finished'            # zero hits
git grep -n 'join_all_clients'            # zero hits
git grep -nE 'mutex_|lock_guard|shared_lock|unique_lock' \
    src/broker/{connect,disconnect,publish,subscribe}_facade.cpp \
    src/broker/broker.cpp \
    src/connection/connection_manager.cpp \
    src/connection/client_handler.cpp
   →  zero hits  (per thread-safety-inventory: facades and main classes
                  carry no locks; locks live inside shared singletons)
```

If any of these greps finds something, the step is not done.

---

## 5. Tests

### Unit tests to add

| File | Test name | What it proves |
|------|-----------|----------------|
| `src/connection/test/connection_session.test.cpp` | `session_owns_subobjects_and_supports_phase_transition` | session construct + phase enum |
| `src/connection/test/client_handler.test.cpp` | `process_accept_job_registers_slot_session_and_reactor_callbacks` | accept side-effects (use a fake reactor, real table) |
|  | `process_decode_job_consumes_complete_packet_and_invokes_facade` | packet routed to broker |
|  | `process_decode_job_returns_on_partial_packet_without_facade_call` | NeedMore path |
|  | `process_drain_job_moves_outbound_messages_to_write_buffer` | outbound path |
|  | `process_close_job_invokes_handle_connection_lost_and_removes_entry` | close path |
|  | `processor_is_safe_under_concurrent_jobs_for_distinct_fds` | no shared state across fds |
| `src/connection/test/connection_manager.test.cpp` | `connection_manager_holds_no_mutex_and_no_thread_per_connection` | static + 100-conn smoke |
|  | `stop_orderly_shuts_down_reactor_then_pool_then_table` | shutdown order observable via fakes |

### Integration tests

- All existing tests must still pass.
- New: `tests/integration_tests/bounded_thread_count_under_load.py` —
  open 1000 simultaneous clients to a running broker and assert
  `len(threads(broker_pid)) ≤ worker_max + 5` at every sample.
- Re-enable: load test 18.1.2 stage 3200 must complete inside the existing
  timeout.

### Manual verification

- macOS: `ps -M $(pgrep yahabroker) | wc -l` while running 18.1.2.
- Linux Pi: `ls /proc/$(pgrep yahabroker)/task | wc -l`.
- TSAN debug build: smoke test 200 clients, no warnings.

---

## 6. Acceptance checklist (paste into PR description)

- [ ] `ConnectionSession` exists, `ConnectionTable::Entry` owns it.
- [ ] `client_handler` is a namespace of stateless free functions; class
      `ClientHandler` is gone.
- [ ] `connect_phase_flow.*` and `runtime_phase_flow.*` are deleted; their
      packet-dispatch logic lives in `decode_step.cpp` /
      `handshake_step.cpp` / `runtime_step.cpp` as pure step functions.
- [ ] `WriteQueue::drain` and `set_sink` removed; `WriteQueue` either deleted
      or reduced to a passive FIFO with zero remaining users in `src/`
      (then deleted).
- [ ] `IoReactor::register_connection` is used for **every** accepted client
      socket. Read callback enqueues `DecodeJob`, write callback enqueues
      `DrainJob`.
- [ ] `Broker::wake_outbound(client_id)` exists and is called by every facade
      that pushes to an `OutboundQueue`. `ActiveConnectionRegistry` carries
      the `fd` per client_id.
- [ ] `ConnectionManager.h ≤ 100 lines`, `.cpp ≤ 80 lines`.
- [ ] `Broker.h ≤ 350 lines`, `.cpp ≤ 165 lines` (already achieved in step 03,
      do not regress).
- [ ] All facades, `Broker`, `ConnectionManager`, `ClientHandler` (now
      namespace), `ConnectionSlot`, `ConnectionSession` carry **no** mutex.
- [ ] All grep proofs from §4 return empty.
- [ ] All unit tests green.
- [ ] All integration tests green, including 18.1.2 stage 3200.
- [ ] Bounded-thread integration test green (≤ `max_threads + 5` at 1000
      simultaneous connections).
- [ ] TSAN smoke (200 clients) clean.

---

## 7. Out of scope for this step

- Scale-down of worker threads at runtime (forbidden by §9).
- TLS — the same model applies; if/when TLS is added, the read/write step
  functions call into the TLS engine instead of `nb_read`/`nb_write`.
- Per-fd batching of read events into one DecodeJob — already handled because
  the JobScheduler coalesces a busy fd's pending submissions into the
  `backlog`.

---

## 8. Implementation order (suggested, one PR per group)

1. **PR-A** — Add `ConnectionSession`, change `ConnectionTable` to
   `Entry { slot, session }`. No behavior change yet (session is constructed
   but unused outside `process_accept_job`). Tests for the table/session.
2. **PR-B** — Add `decode_step`, `handshake_step`, `runtime_step`,
   `outbound_drain_step`, `close_step`. Cover each with unit tests using
   in-memory fakes. **Do not wire them yet.**
3. **PR-C** — Rewrite `client_handler` as the four free functions; rewrite
   `ConnectionManager`; wire everything; delete `client_handler` class,
   `connect_phase_flow.*`, `runtime_phase_flow.*`. Deletions of
   `WriteQueue` happen here too. Update `main.cpp` and `Broker` plumbing
   (`wake_outbound`, fd in registry, `set_job_scheduler`).
4. **PR-D** — Cleanup pass: run the §4 greps; delete dead helpers;
   final line-count audit; add the bounded-thread integration test;
   re-enable 18.1.2 stage 3200.

After PR-D the threading refactoring is complete per
`threading-refactoring.md` §15.
