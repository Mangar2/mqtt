# executor — Executor Primitives (Threading Refactoring Step 02)

Standalone execution module for connection jobs.
Depends on: `network/connection_table` from step 01.

This module is intentionally not integrated into `Broker` or
`ConnectionManager` yet.

---

## Responsibilities

- Define a value-type job model (`ConnectionJob`).
- Provide a concurrent blocking FIFO for jobs (`JobQueue`).
- Serialize execution per connection fd (`JobScheduler`).
- Execute jobs on an elastic worker pool (`WorkerPool`).
- Apply pure scaling policy decisions (`pool_scaling_policy`).

---

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `connection_job.h` | step 02 | Job value type with `JobType` and payload variant |
| `job_queue.h/.cpp` | step 02 | Blocking concurrent FIFO queue for jobs |
| `pool_scaling_policy.h/.cpp` | step 02 | Pure `should_grow(...)` scaling decision |
| `job_scheduler.h/.cpp` | step 02 | Per-fd serialization and backlog scheduling |
| `worker_pool.h/.cpp` | step 02 | Elastic worker threads + scheduler + queue orchestration |

---

## Public API

### `ConnectionJob`

- `JobType { Accept, Decode, Drain, Close }`
- fields:
  - `type`
  - `connection_fd`
  - `payload` (`std::variant<AcceptJobPayload, DecodeJobPayload, DrainJobPayload, CloseJobPayload>`)

Thread safety: value type, no lock.

### `JobQueue`

```cpp
class JobQueue {
public:
    void push(ConnectionJob job);
    std::optional<ConnectionJob> pop_blocking();
    std::size_t size() const noexcept;
    void shutdown();
};
```

- `pop_blocking()` waits until a job arrives or shutdown is requested.
- after shutdown, returns `std::nullopt` when the queue has drained.

### `JobScheduler`

```cpp
class JobScheduler {
public:
    explicit JobScheduler(JobQueue& queue) noexcept;
    void submit(ConnectionJob job);
    std::optional<ConnectionJob> mark_done(int connection_fd);
};
```

- Ensures at most one active job per connection fd.
- Additional jobs for the same fd are buffered in per-fd backlog.
- `mark_done(fd)` returns the next backlog job if present.
- Internal tracing metadata formatting (job-type names) is available in all
    build modes, including tracing-disabled/coverage configurations.

### `WorkerPool`

```cpp
class WorkerPool {
public:
    using JobHandler = std::function<void(const ConnectionJob&)>;

    explicit WorkerPool(JobHandler job_handler, std::size_t max_threads = 0U);
    void start(std::size_t min_threads);
    void stop();
    void submit(ConnectionJob job);

    std::size_t worker_count() const noexcept;
    std::size_t busy_count() const noexcept;
    bool is_running() const noexcept;
};
```

- Workers block on `JobQueue::pop_blocking()`.
- Pool grows only upward during lifetime.
- `stop()` shuts down queue and joins all threads.

### `should_grow(...)`

```cpp
bool should_grow(double queue_depth_avg,
                 std::size_t worker_count,
                 double busy_ratio,
                 std::size_t max_threads) noexcept;
```

Scale-up rule:
- `queue_depth_avg > worker_count * 2`
- `busy_ratio > 0.85`
- `worker_count < max_threads`

