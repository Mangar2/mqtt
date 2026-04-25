# monitoring/test — TEST_SPEC.md

Unit tests for Module 16: Monitoring.

## Test file

`monitoring_test.cpp`

## Test cases

### StatisticsCollector (Module 16.1)

| Test case | Behaviour |
|-----------|-----------|
| `stats_initial_snapshot_is_zero` | All counters are zero after construction; uptime ≥ 0. |
| `stats_client_connect_disconnect` | `on_client_connected` / `on_client_disconnected` correctly track net client count. |
| `stats_message_throughput` | `on_message_inbound` / `on_message_outbound` counters accumulate correctly. |
| `stats_subscription_count_from_store` | `active_subscriptions` in snapshot reflects the SubscriptionStore size. |
| `stats_retained_count_from_store` | `retained_messages` in snapshot reflects the RetainedMessageStore size. |
| `stats_uptime_increases` | Uptime in snapshot is > 0 after a brief simulated delay (test using counter, not real sleep). |

### SysTopicPublisher (Module 16.2)

| Test case | Behaviour |
|-----------|-----------|
| `sys_publisher_zero_interval_no_publish` | A zero-second interval never publishes. |
| `sys_publisher_first_tick_publishes` | First `tick()` with a positive interval and `now` far in the future publishes immediately. |
| `sys_publisher_interval_not_elapsed` | `tick()` returns `false` when called before the interval has elapsed. |
| `sys_publisher_interval_elapsed` | `tick()` returns `true` and publishes when interval has elapsed. |
| `sys_publisher_publishes_all_sys_topics` | All six `$SYS/broker/…` topics are published. |
| `sys_publisher_payload_is_decimal` | Published payload bytes decode to the correct decimal string. |
| `sys_publisher_retain_flag_set` | All published messages have `retain = true`. |
| `sys_publisher_qos_at_most_once` | All published messages use `QoS::AtMostOnce`. |

### StructuredTracer (Module 26)

| Test case | Behaviour |
|-----------|-----------|
| `tracer_emits_json_line_with_mandatory_fields` | `emit()` writes exactly one JSON object line containing `timestamp`, `level`, `module`, `info`, `theme_count`, and `theme_rate_per_second`. |
| `tracer_emits_optional_detail_and_data` | `detail` and `data` are included only when provided. |
| `tracer_global_hierarchy_filters_non_trace_levels` | Global threshold controls `error`, `warning`, and `info` hierarchically. |
| `tracer_trace_module_override_works_with_global_error` | With global `error`, trace events are emitted only for explicitly enabled modules. |
| `tracer_none_disables_all_output` | Global `none` suppresses all events, including module-level trace overrides. |
| `tracer_emits_theme_count_and_rate_per_second` | Each emitted event includes per-`info` theme counter and a two-window approximation for traces per second. |
| `tracer_serialization_failure_falls_back_to_minimal_record` | Stream serialization failures degrade to a minimal error record instead of throwing. |
| `trace_level_roundtrip_and_case_insensitive_parse` | `to_string` and `parse_trace_level` cover all levels, including case-insensitive input. |
| `tracer_set_trace_modules_and_escape_sequences` | Module override replacement works and JSON string escaping handles `\\`, `"`, `\n`, `\r`, and `\t`. |
