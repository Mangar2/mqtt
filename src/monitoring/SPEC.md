# monitoring/ — Module 16: Monitoring

Runtime observability of the MQTT 5.0 broker.
Depends on: `store/` (Module 4), `message_router/` (Module 12), `broker/` (Module 15).

---

## Purpose

Provide statistics counters and periodic $SYS topic publication so that
external clients can observe the broker's operational state.

---

## Files

| File                        | Plan ref | Description |
|-----------------------------|----------|-------------|
| `statistics_collector.h/cpp` | 16.1     | Atomic counters for client connections, message throughput, uptime; direct store queries for subscription and retained message counts. |
| `sys_topic_publisher.h/cpp`  | 16.2     | Publishes a snapshot of statistics to `$SYS/broker/…` topics at a configurable interval. |
| `trace_level.h/cpp`          | 26.2, 26.3 | Trace level enum and parser for global/per-module filtering configuration. |
| `structured_tracer.h/cpp`    | 26.1, 26.3, 26.5 | JSON-lines tracer with hierarchical filtering, per-module trace override, and graceful fallback on serialisation failure. |
| `trace_runtime_command.h/cpp` | 26.4 | Runtime `$SYS` tracing command parser that applies global and per-module overrides to `StructuredTracer`. |

---

## 16.1 StatisticsCollector

### Public API

```cpp
StatisticsCollector(const SubscriptionStore&, const RetainedMessageStore&);

void on_client_connected()    noexcept;   // 16.1.1 — called on register_connection
void on_client_disconnected() noexcept;   // 16.1.1 — called on unregister_connection
void on_message_inbound()     noexcept;   // 16.1.2 — called when a PUBLISH is routed
void on_message_outbound()    noexcept;   // 16.1.2 — called for each delivery

[[nodiscard]] StatisticsSnapshot snapshot() const noexcept;   // 16.1.1–16.1.5
```

### StatisticsSnapshot

```cpp
struct StatisticsSnapshot {
    std::size_t   connected_clients;    // 16.1.1
    std::uint64_t messages_inbound;     // 16.1.2
    std::uint64_t messages_outbound;    // 16.1.2
    std::size_t   active_subscriptions; // 16.1.3  — queried from SubscriptionStore
    std::size_t   retained_messages;    // 16.1.4  — queried from RetainedMessageStore
    std::chrono::seconds uptime;        // 16.1.5
};
```

### Thread safety

All `on_*` increment methods and `snapshot()` are thread-safe (use
`std::atomic` with `relaxed` ordering for throughput counters).
Store queries (`sub_store_.size()`, `retained_store_.size()`) are read-only
and safe when the broker is single-threaded.

---

## 16.2 SysTopicPublisher

### Published $SYS topics (16.2.1)

| Topic                                  | Value                    |
|----------------------------------------|--------------------------|
| `$SYS/broker/clients/connected`        | Connected client count   |
| `$SYS/broker/messages/received`        | Total inbound messages   |
| `$SYS/broker/messages/sent`            | Total outbound messages  |
| `$SYS/broker/subscriptions/count`      | Active subscriptions     |
| `$SYS/broker/retained messages/count`  | Retained message count   |
| `$SYS/broker/uptime`                   | Uptime in seconds        |

All messages are published with QoS 0 and `retain = true` so that newly
connecting subscribers immediately receive the last known value.

### Public API

```cpp
SysTopicPublisher(const StatisticsCollector&, std::chrono::seconds interval,
                  PublishFn publish_fn);
bool tick(std::chrono::steady_clock::time_point now = steady_clock::now());
```

Caller must invoke `tick()` regularly from the event loop.
`tick()` publishes if `interval > 0` and the interval has elapsed since the
last publish; returns `true` when a publish occurred.

### System topic exclusion (16.2.3)

All topics begin with `$SYS/`, so the Topic Engine's system-topic rule
(Module 3.3.4) already excludes them from `#`/`+` wildcard subscriptions.
Only clients with explicit `$SYS/…` subscription filters receive them.

### Interval configuration (16.2.2)

The publish interval is set via `BrokerConfig::sys_topic_interval`.
A value of `0` disables `$SYS` publication entirely.

---

## Broker wiring (Module 15 integration)

`Broker::create_modules()` creates `StatisticsCollector` and
`SysTopicPublisher`.  The broker:
- Calls `stats_->on_client_connected()` in `register_connection()`.
- Calls `stats_->on_client_disconnected()` in `unregister_connection()`.
- Wraps the `DeliverFn` callback to call `stats_->on_message_outbound()`.
- Exposes `handle_publish()` which calls `stats_->on_message_inbound()` before
  delegating to `MessageRouter::route()`.
- Exposes `tick()` for the main loop; delegates to `sys_publisher_->tick()`.

---

## Module 26 — Structured Tracing

### Trace event format (26.1)

- Exactly one JSON object per line.
- Mandatory fields: `timestamp`, `level`, `module`, `info`, `theme_count`, `theme_rate_per_second`.
- Optional fields: `detail`, `data`.

`data` is encoded as an object of string key/value pairs and is primarily used
for trace-level diagnostics.

`theme_count` is the total number of emitted trace records with the same
`info` theme name since process start.

`theme_rate_per_second` is a per-theme approximation based on two 1-second
window start points tracked per `info` theme:

- `t1`: start of older window with saved counter `count(t1)`.
- `t2`: start of newer window with saved counter `count(t2)`.
- First event for a theme sets `t1 = now`.
- If at least 1 second elapsed since `t1`, a subsequent event sets `t2 = now`.
- If `t2` exists and at least 1 second elapsed since `t2`, a subsequent event
  shifts windows: `t1 = t2`, `t2 = now`.
- Output rate formula: `(count(t2) - count(t1)) / (t2 - t1)`.
- Before `t2` exists, `theme_rate_per_second` is `0.0`.

### Trace levels and filtering (26.2, 26.3)

- Supported levels: `none`, `error`, `warning`, `info`, `trace`.
- Hierarchy: `trace` > `info` > `warning` > `error` > `none`.
- `error`, `warning`, `info` follow one global threshold.
- `trace` is emitted when either:
  - global level is `trace`, or
  - the event module is in the explicit trace-module override set.
- `none` disables all output.

### Infrastructure and resilience (26.5)

`StructuredTracer` writes to a configured `std::ostream` sink. Callers provide
typed event data (`TraceEvent`) and do not build JSON manually.

If JSON serialisation throws, the tracer writes a minimal fallback error record
and does not propagate the failure into broker runtime control flow.
