# will_manager — Test Specification (Module 11)

All tests use Catch2 v3 and the `[will_manager]` tag.

---

## WillStore (11.1)

| # | Test name | Scenario | Input | Expected |
|---|-----------|----------|-------|----------|
| 1 | `will_store_store_and_load` | Store a will and load it back | `store("c1", will)` then `load("c1")` | Returns stored `WillMessage` |
| 2 | `will_store_load_absent` | Load non-existent entry | `load("missing")` | `std::nullopt` |
| 3 | `will_store_remove_exists` | Remove an existing entry | `store("c1", will)`, `remove("c1")`, `load("c1")` | `std::nullopt` after remove |
| 4 | `will_store_remove_noop` | Remove non-existent entry | `remove("missing")` | No exception; `size() == 0` |
| 5 | `will_store_overwrite` | Store replaces an existing entry | `store("c1", will1)`, `store("c1", will2)`, `load("c1")` | Returns `will2` |
| 6 | `will_store_size` | Size reflects entry count | Store 2 entries, remove 1 | `size() == 1` |

---

## WillDelayTimer (11.2)

| # | Test name | Scenario | Input | Expected |
|---|-----------|----------|-------|----------|
| 7  | `will_delay_timer_schedule_and_collect` | Timer fires after delay | `schedule("c1", t0, 5)`, `collect_due(t0 + 5s)` | `["c1"]` |
| 8  | `will_delay_timer_not_yet_due` | Timer not yet elapsed | `schedule("c1", t0, 5)`, `collect_due(t0 + 4s)` | Empty vector |
| 9  | `will_delay_timer_zero_delay` | Zero delay fires immediately | `schedule("c1", t0, 0)`, `collect_due(t0)` | `["c1"]` |
| 10 | `will_delay_timer_cancel` | Cancelled timer not returned | `schedule("c1", t0, 0)`, `cancel("c1")`, `collect_due(t0)` | Empty vector |
| 11 | `will_delay_timer_cancel_noop` | Cancel absent entry is no-op | `cancel("missing")` | No exception |
| 12 | `will_delay_timer_overwrite` | Schedule replaces existing entry | `schedule("c1", t0, 10)`, `schedule("c1", t0, 2)`, `collect_due(t0 + 2s)` | `["c1"]` |
| 13 | `will_delay_timer_size` | Size reflects entry count | Schedule 3, cancel 1 | `size() == 2` |
| 14 | `will_delay_timer_collect_does_not_remove` | `collect_due` leaves entries in place | `schedule("c1", t0, 0)`, `collect_due(t0)` twice | Second call also returns `["c1"]` |

---

## WillPublisher (11.3)

| # | Test name | Scenario | Input | Expected |
|---|-----------|----------|-------|----------|
| 15 | `will_publisher_on_connect_stores_will` | Connect stores will | `on_connect("c1", will)` | `will_store.load("c1")` is present |
| 16 | `will_publisher_on_reconnect_cancels_timer` | Reconnect cancels timer | `on_connection_lost("c1", t0)`, `on_reconnect("c1")`, `publish_due(t0)` | Callback not invoked |
| 17 | `will_publisher_on_disconnect_normal_suppresses` | Normal disconnect (0x00) suppresses will | `on_connect`, `on_disconnect(reason=Success, t0)` | Callback never invoked; will removed |
| 18 | `will_publisher_on_disconnect_with_will_arms_timer` | Disconnect 0x04 arms delay timer with non-zero delay | `on_connect` (delay=5), `on_disconnect(reason=0x04, t0)`, `publish_due(t0+4s)` | Callback not invoked at t0+4s |
| 19 | `will_publisher_on_disconnect_with_will_publishes_after_delay` | Disconnect 0x04 publishes after delay | `on_connect` (delay=5), `on_disconnect(reason=0x04, t0)`, `publish_due(t0+5s)` | Callback invoked once with correct will |
| 20 | `will_publisher_on_disconnect_zero_delay_publishes_immediately` | Disconnect 0x04 with zero delay publishes immediately | `on_connect` (delay=0), `on_disconnect(reason=0x04, t0)` | Callback invoked during `on_disconnect` |
| 21 | `will_publisher_on_connection_lost_arms_timer` | Connection lost arms timer | `on_connect` (delay=2), `on_connection_lost(t0)`, `publish_due(t0+1s)` | Callback not invoked at t0+1s |
| 22 | `will_publisher_on_connection_lost_publishes_after_delay` | Connection lost publishes after delay | `on_connect` (delay=2), `on_connection_lost(t0)`, `publish_due(t0+2s)` | Callback invoked once |
| 23 | `will_publisher_on_connection_lost_zero_delay_publishes` | Connection lost with zero delay publishes immediately | `on_connect` (delay=0), `on_connection_lost(t0)` | Callback invoked immediately |
| 24 | `will_publisher_on_session_expired_publishes_pending_will` | Session expiry triggers immediate publish | `on_connect` (delay=10), `on_connection_lost(t0)`, `on_session_expired("c1")` | Callback invoked; timer cancelled |
| 25 | `will_publisher_on_session_expired_no_will_noop` | Session expiry with no will is no-op | `on_session_expired("c1")` | Callback not invoked; no exception |
| 26 | `will_publisher_publish_due_removes_state` | `publish_due` clears will and timer | `on_connect` (delay=0), `on_connection_lost(t0)`, `publish_due(t0)` twice | Callback invoked once |
| 27 | `will_publisher_on_disconnect_no_will_noop` | Disconnect with no stored will is no-op | `on_disconnect(reason=0x04, t0)` | No exception; callback not invoked |
| 28 | `will_publisher_on_connection_lost_no_will_noop` | Connection lost with no stored will is no-op | `on_connection_lost(t0)` | No exception; callback not invoked |
