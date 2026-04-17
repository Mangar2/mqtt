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
