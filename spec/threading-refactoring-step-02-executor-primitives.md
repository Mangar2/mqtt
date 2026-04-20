# Step 02 — Executor Primitives (standalone Worker-Pool)

Bezug: `threading-refactoring.md` §5.2, §9, §14

## Ziel

Eigenständiges `executor`-Modul: Job-Typ, Queue, Scheduler, Scaling-Policy, WorkerPool. Komplett mit Unit-Tests. Noch nicht in Broker / ConnectionManager integriert. Nach diesem Schritt: Broker unverändert, lauffähig, compileable.

## Warum als zweites

- Hängt nur von `connection_table` (Step 01) ab.
- Threading-Mechanik (Pool, Scheduler) wird isoliert validiert, bevor sie produktiv geschaltet wird.
- Erlaubt späteren Reactor-/Broker-Integrationen klare API.

## Zu erstellende Dateien

| Datei | Zeilen Ziel | Inhalt |
|-------|-------------|--------|
| `src/executor/connection_job.h` | ~40 | Value-Type. Enum `JobType { Accept, Decode, Drain, Close }`, Felder `int connection_fd`, `std::variant<...>` Payload. Kein Lock, kein State. |
| `src/executor/job_queue.h` | ~50 | Concurrent FIFO über `ConnectionJob`. API: `push(job)`, `pop_blocking() -> optional<ConnectionJob>`, `size()`, `shutdown()`. Internes `std::mutex` + `std::condition_variable`. |
| `src/executor/job_queue.cpp` | ~80 | Implementierung. `pop_blocking` wartet bis Job oder shutdown. |
| `src/executor/pool_scaling_policy.h` | ~40 | Pure Funktion: `bool should_grow(queue_depth_avg, worker_count, busy_ratio, max_threads)`. Konstanten aus §9. Kein Lock. |
| `src/executor/pool_scaling_policy.cpp` | ~60 | Schwellwerte (`queue_depth_avg > worker_count * 2 && busy_ratio > 0.85`). |
| `src/executor/job_scheduler.h` | ~60 | Per-Connection Serialisierung. API: `submit(ConnectionJob)`, `mark_done(fd) -> optional<ConnectionJob>` (gibt nächsten gepufferten Job zurück, falls vorhanden). Verhindert parallele Jobs auf derselben fd. Internes `std::mutex` für Map. |
| `src/executor/job_scheduler.cpp` | ~100 | Map `fd -> { active: bool, deque<ConnectionJob> backlog }`. Bei submit: wenn nicht active → in JobQueue + active=true; sonst in backlog. Bei mark_done: nächsten backlog-Job zurückgeben oder active=false. |
| `src/executor/worker_pool.h` | ~80 | Elastischer Pool. API: `start(min_threads)`, `stop()`, `submit(ConnectionJob)`. Eigene Worker-Threads. Internes `mutex_` + `cv_`, atomare Counter (`busy_count_`, `worker_count_`). |
| `src/executor/worker_pool.cpp` | ~150 | Worker-Loop: pop aus JobQueue, Counter inkrementieren, Job-Handler aufrufen (callable, in Step 05 wirelet), Counter dekrementieren. Scaling-Tick alle 250ms (eigener Timer-Thread oder beim pop). `stop()` ruft `JobQueue::shutdown()` und join-t alle Worker. |
| `src/executor/SPEC.md` | — | Modulübersicht, API, Scaling-Regeln. |

## Unit-Tests

### `connection_job.test.cpp`
- `job_constructed_with_type_and_fd_holds_values`.
- `job_payload_variant_round_trips_for_each_type`.

### `job_queue.test.cpp`
- `push_then_pop_returns_same_job`.
- `pop_blocks_until_push` — Thread blockiert in pop, anderer pusht, Job wird empfangen.
- `pop_returns_nullopt_after_shutdown`.
- `fifo_order_preserved_under_concurrent_push` — 4 Producer, 1 Consumer, alle Jobs erhalten.
- `size_reflects_pending_jobs`.

### `pool_scaling_policy.test.cpp`
- `does_not_grow_when_queue_empty`.
- `does_not_grow_when_busy_ratio_below_threshold`.
- `grows_when_queue_deep_and_workers_busy`.
- `does_not_grow_above_max_threads`.

### `job_scheduler.test.cpp`
- `submit_for_idle_connection_enqueues_immediately`.
- `submit_for_busy_connection_buffers_in_backlog`.
- `mark_done_returns_next_backlog_job_when_pending`.
- `mark_done_returns_nullopt_when_no_backlog`.
- `at_most_one_active_job_per_fd_under_concurrent_submit` — Stress-Test mit 100 fds, je 1000 submits, count(active per fd) ≤ 1.

### `worker_pool.test.cpp`
- `start_creates_min_threads`.
- `submit_executes_job_on_worker` — Job-Handler (per Konstruktor) zählt Aufrufe.
- `stop_drains_pending_and_joins_all_workers`.
- `pool_grows_under_sustained_load` — 1000 langsame Jobs (sleep 1ms), Pool wächst über min_threads, nicht über max.
- `pool_does_not_shrink_during_lifetime`.
- `no_thread_leak_after_stop` — Thread-Counter vor start == nach stop.

## Akzeptanzkriterien

1. Neues CMake-Target `executor` (oder Eintrag in bestehende Struktur), kompiliert standalone.
2. Alle Unit-Tests grün via `python test/run_coverage.py`.
3. Bestehende Unit- und Integrationstests unverändert grün.
4. Broker- und ConnectionManager-Quellen **unverändert**.
5. Zeilenlimits aus Tabelle eingehalten.
6. Locks ausschließlich in `job_queue`, `job_scheduler`, `worker_pool`. Keine Locks in `connection_job`, `pool_scaling_policy`.
7. ThreadSanitizer-Lauf der executor-Tests sauber.

## Out of Scope

- Keine Integration in Broker / ConnectionManager.
- Kein IoReactor.
- Kein realer Job-Handler — wird in Step 05 angekoppelt. Hier nur Test-Doubles.
