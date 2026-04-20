# Threading Model Refactoring — Reactor + Worker Pool

## 1. Motivation

Profiling under load (test 18.1.2, stage 3200) shows:
- Thread count explodes: 2 threads per connection (client + drain) = 6400 threads at 3200 connections.
- `ConnectionManager::cleanup_finished()` joins dead threads synchronously in the accept path.
- `std::thread::join()` dominates CPU time under load.
- `broker_mutex_` (shared_mutex) locks at the top level for every publish, subscribe, connect, disconnect.

Root cause: thread-per-connection model with synchronous cleanup and coarse top-level locking.

## 2. Design Rules

1. **Main classes must shrink.** Broker (currently 525h + 1009cpp), ConnectionManager (129h + 181cpp) must have fewer lines after refactoring. Not one line more. No exceptions.
2. **No locks in main classes.** Broker, ConnectionManager must hold zero mutexes. All locking lives inside small dedicated helper classes.
3. **Small helper classes.** Each new class has one responsibility, one file pair, own internal lock if needed. Target: under 150 lines per .cpp.
4. **Threads don't wait on inbound I/O.** No thread blocks on `recv()` or `accept()`. Only the reactor waits on kernel events.
5. **Threads get work only when work exists.** Workers sleep on condition variable, wake on job arrival.
6. **Thread creation only under sustained load.** Pool starts with min threads, grows only when queue pressure exceeds threshold.
7. **Threads live until program end.** No thread join during runtime. Only at shutdown.

## 3. Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                      Broker                          │
│  (thin orchestrator — no mutex, no threads,          │
│   delegates to facades and registries)               │
└──────────┬───────────────────────────────┬──────────┘
           │                               │
    ┌──────▼──────┐                 ┌──────▼──────┐
    │  Facades    │                 │  Registries │
    │ (each with  │                 │ (each with  │
    │  own lock)  │                 │  own lock)  │
    └──────┬──────┘                 └──────┬──────┘
           │                               │
    ┌──────▼───────────────────────────────▼──────┐
    │              ConnectionManager               │
    │  (thin: owns Reactor + WorkerPool,           │
    │   no mutex, no thread join in hot path)      │
    └──────┬──────────────────────────┬───────────┘
           │                          │
    ┌──────▼──────┐           ┌──────▼──────┐
    │  IoReactor  │           │ WorkerPool  │
    │ (1-2 threads│           │ (N threads, │
    │  kqueue/    │           │  elastic,   │
    │  epoll)     │           │  job queue) │
    └──────┬──────┘           └──────┬──────┘
           │                          │
    ┌──────▼──────┐           ┌──────▼──────┐
    │ Connection  │           │    Job      │
    │ Table       │           │  Scheduler  │
    │ (slot per   │           │ (per-conn   │
    │  socket)    │           │  serialize) │
    └─────────────┘           └─────────────┘
```

## 4. Data Flow

### Inbound (client → broker)

1. **IoReactor** detects read-ready via kqueue/epoll.
2. IoReactor reads bytes non-blocking into the `ConnectionSlot` read buffer.
3. IoReactor submits a `DecodeJob` to `JobScheduler`.
4. `JobScheduler` ensures at most one active job per connection (serialization flag).
5. **WorkerPool** picks up the job.
6. Worker decodes MQTT frame from read buffer.
7. Worker calls the appropriate broker facade (publish/subscribe/etc.).
8. Worker encodes response into `ConnectionSlot` write buffer.
9. Worker notifies IoReactor of write-ready interest.
10. **IoReactor** sends bytes non-blocking when socket is writable.

### Outbound (broker → client, e.g. routed publish)

1. Broker facade pushes message into per-connection `OutboundQueue`.
2. `OutboundQueue::push()` submits a `DrainJob` to `JobScheduler`.
3. Worker picks up drain job, encodes frames into write buffer.
4. Worker notifies IoReactor of write-ready interest.
5. IoReactor sends bytes non-blocking.

## 5. New Classes — Location and Responsibility

### 5.1 Network Layer — `src/network/`

| New File | Lines (target) | Responsibility | Lock |
|----------|---------------|----------------|------|
| `io_reactor.h/.cpp` | ~120h / ~200cpp | kqueue/epoll event loop. Owns 1-2 threads. Dispatches read/write/accept events. Platform-abstracted via `io_reactor_kqueue.cpp` / `io_reactor_epoll.cpp`. | Internal mutex for event registration changes only. Hot path (dispatch) is single-threaded, lock-free. |
| `connection_slot.h/.cpp` | ~80h / ~100cpp | Per-connection I/O state: socket handle, read ring buffer, write ring buffer, connection-phase flag (connecting/connected/closing). No thread. No lock (accessed only by one job at a time, enforced by JobScheduler). | None — serialized access via JobScheduler. |
| `connection_table.h/.cpp` | ~60h / ~80cpp | Owns all `ConnectionSlot` instances. Indexed by socket fd. Add/remove/lookup. | Internal shared_mutex for add/remove. Lookup by fd is read-lock. |
| `socket_ops.h/.cpp` | ~40h / ~60cpp | Non-blocking socket helpers: `set_nonblocking()`, `nb_read()`, `nb_write()`, `nb_accept()`. Pure functions, no state, no lock. | None. |

### 5.2 Executor Layer — `src/executor/` (new module)

| New File | Lines (target) | Responsibility | Lock |
|----------|---------------|----------------|------|
| `worker_pool.h/.cpp` | ~80h / ~150cpp | Elastic thread pool. Owns worker threads. Threads sleep on condition variable. Scale-up on sustained queue pressure. No scale-down until shutdown. | Internal mutex + cv for job queue. |
| `job_queue.h/.cpp` | ~50h / ~80cpp | Concurrent FIFO queue for `ConnectionJob` items. Lock-free or fine-grained mutex. | Internal mutex (or lock-free). |
| `job_scheduler.h/.cpp` | ~60h / ~100cpp | Per-connection job serialization. Tracks which connections have an active job. Prevents parallel execution on the same connection. Enqueues deferred jobs when a connection is already busy. | Internal mutex for the scheduling map. |
| `connection_job.h` | ~40h | Job type enum + data: `{job_type, connection_fd, payload}`. Types: `Accept`, `Decode`, `Drain`, `Close`. Value type, no lock. | None. |
| `pool_scaling_policy.h/.cpp` | ~40h / ~60cpp | Decides when to add a worker thread. Inputs: queue depth, worker count, busy ratio. Output: bool should_scale_up. Pure logic, no lock. | None. |

### 5.3 Broker Facades — `src/broker/` (extracted from Broker)

Each facade owns its internal state and lock. Broker delegates to them without holding any mutex.

| New File | Lines (target) | Extracted from Broker | Lock |
|----------|---------------|----------------------|------|
| `connect_facade.h/.cpp` | ~60h / ~180cpp | `handle_connect()`, `handle_auth_packet()`, `handle_reauthenticate()`, `complete_connect_success()`, `emit_connect_trace()` | Internal mutex — covers session + will + auth state for connect path only. |
| `disconnect_facade.h/.cpp` | ~40h / ~100cpp | `handle_disconnect()`, `handle_connection_lost()`, `is_disconnect_expiry_override_valid()` | Internal mutex — covers will + session + unregister for disconnect path only. |
| `publish_facade.h/.cpp` | ~40h / ~120cpp | `handle_publish()` | Internal mutex — covers message_router + stats for publish path only. |
| `subscribe_facade.h/.cpp` | ~40h / ~100cpp | `handle_subscribe()`, `handle_unsubscribe()` | Internal mutex — covers subscription_orchestrator for subscribe path only. |
| `enhanced_auth_registry.h/.cpp` | ~50h / ~60cpp | `pending_enhanced_auth_`, `active_enhanced_auth_` maps + lookup/insert/erase | Internal mutex — auth state only. |
| `broker_module_factory.h/.cpp` | ~30h / ~180cpp | `create_modules()` | None — called once at startup. |
| `persistence_coordinator.h/.cpp` | ~30h / ~80cpp | `load_persistence()`, `flush_persistence()` | None — called at startup/shutdown only. |
| `tick_handler.h/.cpp` | ~30h / ~60cpp | `tick()`, `apply_trace_system_message()` | Internal mutex — covers will timer + session expiry + sys publisher. |

### 5.4 Connection Layer — `src/connection/` (refactored)

| File | Change | Result |
|------|--------|--------|
| `connection_manager.h/.cpp` | Remove accept loop threads, client thread tracking, cleanup_finished, join logic. Becomes thin owner of IoReactor + WorkerPool. `start()` starts reactor and pool. `stop()` stops both. | Shrinks from 129h+181cpp to ~50h+60cpp. |
| `client_handler.h/.cpp` | Remove thread ownership, drain thread spawn. Becomes stateless job processor: `process_decode_job(ConnectionSlot&, Broker&)`, `process_drain_job(ConnectionSlot&)`. | Stays small, changes shape. |
| `connection_session.h/.cpp` (new) | Per-connection session state that currently lives on the client thread stack: `ClientSession`, `WriteQueue`, `StreamBuffer`, `TopicAliasTable`, `ConnectResult`. Heap-allocated, owned by ConnectionSlot or ConnectionTable. | ~80h / ~60cpp. No lock (serialized by JobScheduler). |

## 6. Broker Shrinkage Ledger

Current Broker: **525 lines .h + 1009 lines .cpp = 1534 total**

| What moves out | Lines removed (actual) | Moves to |
|----------------|----------------------|----------|
| `handle_connect` + `handle_auth_packet` + `handle_reauthenticate` + `complete_connect_success` + `map_auth_error_to_reason` + `protocol_error_result` + `emit_connect_trace` | 242cpp + ~60h | `connect_facade` |
| `handle_disconnect` + `handle_connection_lost` + `is_disconnect_expiry_override_valid` | 52cpp + ~25h | `disconnect_facade` |
| `handle_publish` | 85cpp + ~15h | `publish_facade` |
| `handle_subscribe` + `handle_unsubscribe` | 60cpp + ~20h | `subscribe_facade` |
| `pending_enhanced_auth_` + `active_enhanced_auth_` maps | ~10cpp + ~15h | `enhanced_auth_registry` |
| `create_modules()` | 239cpp + ~5h | `broker_module_factory` |
| `load_persistence` + `flush_persistence` | 48cpp + ~5h | `persistence_coordinator` |
| `tick` + `apply_trace_system_message` | 22cpp + ~10h | `tick_handler` |
| `broker_mutex_` declaration + all lock_guard lines | ~15cpp + ~5h | Eliminated entirely |
| `register_connection_locked` + `unregister_connection_locked` | 70cpp + ~10h | Already in `active_connection_registry`, traces move to facades |

**Total removed: ~843cpp + ~170h = ~1013 lines**

Broker after refactoring target: **~355h + ~166cpp = ~521 total** (66% reduction).

## 7. ConnectionManager Shrinkage Ledger

Current: **129h + 181cpp = 310 total**

| What moves out | Lines removed | Moves to |
|----------------|--------------|----------|
| `accept_loop` + `spawn_accept_loop` | ~40cpp + ~15h | Replaced by `IoReactor` accept handling |
| `client_threads_` vector + `ClientThreadEntry` struct | ~15h | Eliminated |
| `cleanup_finished` + `join_all_clients` + `join_accept_threads` | ~40cpp | Eliminated |
| Thread join timeout logic in `stop()` | ~25cpp | Eliminated (pool stop is simple) |

**Total removed: ~105cpp + ~30h = ~135 lines**

ConnectionManager after: **~100h + ~75cpp = ~175 total** (44% reduction).

## 8. Lock Placement Map

| Lock location | What it protects | Scope |
|---------------|-----------------|-------|
| `ActiveConnectionRegistry::mutex_` | online connection map | connection upsert/remove/find |
| `EnhancedAuthRegistry::mutex_` | pending + active enhanced auth maps | auth state per connect flow |
| `ConnectFacade::mutex_` | session creation + will storage during connect | single connect call |
| `DisconnectFacade::mutex_` | will trigger + session cleanup during disconnect | single disconnect call |
| `PublishFacade::mutex_` | message routing | single publish call |
| `SubscribeFacade::mutex_` | subscription store mutation | single subscribe call |
| `TickHandler::mutex_` | will timer + session expiry + sys publish | single tick call |
| `JobScheduler::mutex_` | per-connection job serialization map | job enqueue/dequeue |
| `JobQueue::mutex_` | job FIFO | push/pop |
| `WorkerPool::mutex_` + `cv_` | worker sleep/wake | idle wait + wake |
| `IoReactor::mutex_` | event registration changes | add/remove fd interest |
| `ConnectionTable::mutex_` | slot add/remove | connection open/close |

**Zero locks in:** Broker, ConnectionManager, ClientHandler, ConnectionSlot, ConnectionJob, PoolScalingPolicy, SocketOps, BrokerModuleFactory, PersistenceCoordinator.

## 9. Elastic Scaling Rules

```
min_threads     = max(2, hardware_concurrency)
max_threads     = hardware_concurrency * 4
scale_up_check  = every 250ms
scale_up_when   = queue_depth_avg > worker_count * 2
                  AND worker_busy_ratio > 0.85
scale_up_step   = +1 thread
scale_down      = never (until shutdown)
```

WorkerPool tracks:
- `queue_depth_avg_250ms`: rolling average of JobQueue size sampled at check interval.
- `worker_busy_ratio`: fraction of workers currently executing a job (atomic counter).

PoolScalingPolicy is a pure function: `bool should_grow(queue_depth_avg, worker_count, busy_ratio, max_threads)`.

## 10. Backpressure

| Layer | Mechanism | Action |
|-------|-----------|--------|
| Per-connection read buffer | Ring buffer with max size (e.g. 256 KiB) | IoReactor removes read interest until buffer drained |
| Per-connection write buffer | Ring buffer with max size (config: `write_queue_max_bytes`) | Publish facade rejects with QuotaExceeded |
| Global job queue | Hard cap on pending jobs | IoReactor stops accepting read events until queue drains |
| Worker pool | Busy ratio > 0.95 sustained | Scale up (until max_threads) |

## 11. Platform Abstraction

| Platform | Reactor backend | File |
|----------|----------------|------|
| macOS / BSD | kqueue | `src/network/io_reactor_kqueue.cpp` |
| Linux | epoll | `src/network/io_reactor_epoll.cpp` |

`io_reactor.h` defines the platform-neutral interface. CMake selects the implementation file based on `CMAKE_SYSTEM_NAME`. Same pattern as existing `tcp_connection_posix.cpp` / `tcp_connection_win32.cpp`.

## 12. Implementation Phases

### Phase A: Extract Broker Facades (no threading change yet)

**Goal:** Broker shrinks. All tests still pass. Same threading model.

1. Create `enhanced_auth_registry.h/.cpp` — move auth maps out of Broker.
2. Create `connect_facade.h/.cpp` — move connect handling, takes references to session_manager, will_publisher, enhanced_auth_registry, structured_tracer.
3. Create `disconnect_facade.h/.cpp` — move disconnect/connection_lost handling.
4. Create `publish_facade.h/.cpp` — move handle_publish.
5. Create `subscribe_facade.h/.cpp` — move handle_subscribe/unsubscribe.
6. Create `tick_handler.h/.cpp` — move tick + trace message handling.
7. Create `broker_module_factory.h/.cpp` — move create_modules.
8. Create `persistence_coordinator.h/.cpp` — move load/flush persistence.
9. Remove `broker_mutex_` from Broker. Each facade has its own mutex.
10. Verify: Broker.h < 350 lines, Broker.cpp < 165 lines.

**Test:** All existing unit + integration tests pass unchanged.

### Phase B: Reactor + Non-blocking I/O (replaces accept loop)

**Goal:** Accept and read/write are non-blocking. Client threads still exist temporarily.

1. Create `socket_ops.h/.cpp` — non-blocking helpers.
2. Create `connection_slot.h/.cpp` — per-connection I/O state.
3. Create `connection_table.h/.cpp` — slot registry.
4. Create `io_reactor.h/.cpp` + platform impl — event loop with accept, read-ready, write-ready.
5. Adapt `ConnectionManager::start()` to launch IoReactor instead of accept threads.
6. IoReactor handles accept and read-ready; temporarily still dispatches to a per-connection thread for processing (bridge mode).

**Test:** All existing tests pass. Reactor handles I/O, threads handle protocol.

### Phase C: Worker Pool (replaces per-connection threads)

**Goal:** No more thread-per-connection. Full reactor + pool model.

1. Create `connection_job.h` — job types.
2. Create `job_queue.h/.cpp` — concurrent queue.
3. Create `job_scheduler.h/.cpp` — per-connection serialization.
4. Create `pool_scaling_policy.h/.cpp` — growth decisions.
5. Create `worker_pool.h/.cpp` — elastic pool.
6. Create `connection_session.h/.cpp` — heap-allocated per-connection state.
7. Remove drain thread from `client_handler.cpp`. Workers encode + IoReactor writes.
8. Remove `client_threads_` vector and all join logic from ConnectionManager.
9. Adapt `client_handler` to stateless job processor.

**Test:** All existing tests pass. Load test 18.1.2 completes all stages. Thread count stays bounded.

### Phase D: Cleanup

1. Remove dead code from ConnectionManager (thread tracking, cleanup_finished).
2. Remove `WriteQueue::run_drain()` (replaced by reactor write path).
3. Final line count audit: every main class must be smaller than before Phase A.

## 13. File Tree After Refactoring

```
src/
├── broker/
│   ├── active_connection_registry.h/.cpp    (exists, unchanged)
│   ├── broker.h/.cpp                        (shrunk: ~350h + ~165cpp)
│   ├── broker_config.h                      (exists, unchanged)
│   ├── broker_error.h                       (exists, unchanged)
│   ├── broker_module_factory.h/.cpp         (NEW ~30h + ~180cpp)
│   ├── connack_properties.h/.cpp            (exists, unchanged)
│   ├── config_loader.h/.cpp                 (exists, unchanged)
│   ├── connect_facade.h/.cpp                (NEW ~60h + ~180cpp)
│   ├── disconnect_facade.h/.cpp             (NEW ~40h + ~100cpp)
│   ├── enhanced_auth_registry.h/.cpp        (NEW ~50h + ~60cpp)
│   ├── persistence_coordinator.h/.cpp       (NEW ~30h + ~80cpp)
│   ├── publish_facade.h/.cpp                (NEW ~40h + ~120cpp)
│   ├── subscribe_facade.h/.cpp              (NEW ~40h + ~100cpp)
│   ├── tick_handler.h/.cpp                  (NEW ~30h + ~60cpp)
│   └── SPEC.md                              (updated)
├── connection/
│   ├── client_handler.h/.cpp                (refactored: stateless job processor)
│   ├── connection_manager.h/.cpp            (shrunk: ~50h + ~60cpp)
│   ├── connection_session.h/.cpp            (NEW ~80h + ~60cpp)
│   ├── connect_phase_flow.h/.cpp            (exists, adapted)
│   ├── runtime_phase_flow.h/.cpp            (exists, adapted)
│   └── SPEC.md                              (updated)
├── executor/                                (NEW module)
│   ├── connection_job.h                     (NEW ~40h)
│   ├── job_queue.h/.cpp                     (NEW ~50h + ~80cpp)
│   ├── job_scheduler.h/.cpp                 (NEW ~60h + ~100cpp)
│   ├── pool_scaling_policy.h/.cpp           (NEW ~40h + ~60cpp)
│   ├── worker_pool.h/.cpp                   (NEW ~80h + ~150cpp)
│   └── SPEC.md                              (NEW)
├── network/
│   ├── connection_slot.h/.cpp               (NEW ~80h + ~100cpp)
│   ├── connection_table.h/.cpp              (NEW ~60h + ~80cpp)
│   ├── io_reactor.h                         (NEW ~120h)
│   ├── io_reactor_kqueue.cpp                (NEW ~200cpp)
│   ├── io_reactor_epoll.cpp                 (NEW ~200cpp)
│   ├── socket_ops.h/.cpp                    (NEW ~40h + ~60cpp)
│   ├── stream_buffer.h/.cpp                 (exists, unchanged)
│   ├── tcp_connection.h/.cpp                (exists, unchanged)
│   ├── tcp_listener.h/.cpp                  (exists, may simplify)
│   ├── write_queue.h/.cpp                   (exists, run_drain removed in Phase D)
│   └── SPEC.md                              (updated)
```

## 14. Dependency Order

```
socket_ops              → (nothing)
connection_slot         → socket_ops, stream_buffer
connection_table        → connection_slot
io_reactor              → connection_table, socket_ops, tcp_listener
connection_job          → (nothing)
job_queue               → connection_job
pool_scaling_policy     → (nothing)
job_scheduler           → job_queue, connection_table
worker_pool             → job_queue, pool_scaling_policy
enhanced_auth_registry  → auth (existing)
connect_facade          → session_manager, will_publisher, enhanced_auth_registry
disconnect_facade       → will_publisher, session_manager, active_connection_registry
publish_facade          → message_router, statistics_collector
subscribe_facade        → subscription_orchestrator
tick_handler            → will_publisher, session_manager, sys_topic_publisher
persistence_coordinator → persistence (existing), stores (existing)
broker_module_factory   → all module constructors
connection_session      → client_session, write_queue, stream_buffer
connection_manager      → io_reactor, worker_pool, job_scheduler
broker                  → all facades, connection_manager, broker_module_factory
```

## 15. Success Criteria

1. **Thread count bounded:** At 3200 connections, total threads ≤ `max_threads + 2` (reactor) + 1 (main) regardless of connection count.
2. **No join in hot path:** Zero `std::thread::join()` calls outside of shutdown.
3. **Broker.h < 350 lines, Broker.cpp < 165 lines.**
4. **ConnectionManager.h < 100 lines, ConnectionManager.cpp < 80 lines.**
5. **Zero mutexes in Broker, ConnectionManager, ClientHandler.**
6. **All existing unit tests pass.**
7. **All existing integration tests pass, including load test 18.1.2 through all stages.**
8. **Load test 18.1.2 completes stage 3200 within timeout.**
