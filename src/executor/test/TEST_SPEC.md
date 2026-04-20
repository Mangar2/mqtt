# executor/test — Unit Test Plan (Threading Refactoring Step 02)

Catch2 tag: `[executor]`.

## connection_job_test.cpp

| Test case | Behaviour |
|-----------|-----------|
| `job_constructed_with_type_and_fd_holds_values` | `ConnectionJob` stores selected type and fd |
| `job_payload_variant_round_trips_for_each_type` | Payload variant preserves values for Accept/Decode/Drain/Close payload types |

## job_queue_test.cpp

| Test case | Behaviour |
|-----------|-----------|
| `push_then_pop_returns_same_job` | Push then pop returns same fd/type/payload |
| `pop_blocks_until_push` | Consumer blocks in `pop_blocking()` until producer pushes |
| `pop_returns_nullopt_after_shutdown` | `pop_blocking()` returns nullopt after shutdown on empty queue |
| `fifo_order_preserved_under_concurrent_push` | Per-producer FIFO order is preserved with 4 producers and 1 consumer |
| `size_reflects_pending_jobs` | `size()` tracks pending jobs correctly |

## pool_scaling_policy_test.cpp

| Test case | Behaviour |
|-----------|-----------|
| `does_not_grow_when_queue_empty` | Empty queue never triggers growth |
| `does_not_grow_when_busy_ratio_below_threshold` | Busy ratio below threshold blocks growth |
| `grows_when_queue_deep_and_workers_busy` | Growth enabled when depth and busy ratio exceed thresholds |
| `does_not_grow_above_max_threads` | Growth disabled at max thread count |

## job_scheduler_test.cpp

| Test case | Behaviour |
|-----------|-----------|
| `submit_for_idle_connection_enqueues_immediately` | First submit for fd goes directly to queue |
| `submit_for_busy_connection_buffers_in_backlog` | Further submit for same active fd is buffered |
| `mark_done_returns_next_backlog_job_when_pending` | `mark_done(fd)` returns next buffered job |
| `mark_done_returns_nullopt_when_no_backlog` | `mark_done(fd)` returns nullopt when no buffered work remains |
| `at_most_one_active_job_per_fd_under_concurrent_submit` | Under concurrent submit/consume, active parallel jobs per fd never exceed 1 |

## worker_pool_test.cpp

| Test case | Behaviour |
|-----------|-----------|
| `start_creates_min_threads` | `start(min_threads)` starts requested worker count (bounded by max) |
| `submit_executes_job_on_worker` | Submitted job is executed by handler |
| `stop_drains_pending_and_joins_all_workers` | `stop()` drains queued work and leaves no workers running |
| `pool_grows_under_sustained_load` | Pool grows above `min_threads` under sustained queue pressure |
| `pool_does_not_shrink_during_lifetime` | Worker count does not decrease before stop |
| `no_thread_leak_after_stop` | After stop, worker and busy counters return to zero |

