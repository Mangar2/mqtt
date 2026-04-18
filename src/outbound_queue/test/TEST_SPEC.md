# OutboundQueue — TEST_SPEC.md

Unit tests for `OutboundQueue` (Module 20.1).

| Test Name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `outbound_queue_default_construction` | Default constructor | None | `is_empty() == true`, `size() == 0`, `is_stopped() == false` |
| `outbound_queue_custom_max_depth` | Construct with custom max | `max_depth = 5` | Queue accepts up to 5 messages |
| `outbound_queue_push_and_try_pop` | Push one message then pop | One `Message` | `try_pop()` returns the same message |
| `outbound_queue_fifo_order` | Push multiple, pop in order | 3 messages | Pop returns messages in push order |
| `outbound_queue_try_pop_empty` | Pop from empty queue | None | `try_pop()` returns `std::nullopt` |
| `outbound_queue_push_full_drops` | Push when at max depth | `max_depth = 2`, push 3 | Third push returns `false`, `size() == 2` |
| `outbound_queue_stop_rejects_push` | Push after stop | Stop then push | `push()` returns `false` |
| `outbound_queue_stop_allows_drain` | Pop after stop | Push, stop, pop | `try_pop()` returns the pushed message |
| `outbound_queue_stop_idempotent` | Call stop twice | Two stop calls | No crash, `is_stopped() == true` |
| `outbound_queue_size_tracks_push_pop` | Size updates correctly | Push 3, pop 1 | `size() == 2` |
| `outbound_queue_is_empty_after_drain` | Drain all messages | Push 2, pop 2 | `is_empty() == true` |
