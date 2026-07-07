# TEST_SPEC — persistence module (Module 13)

## Unit Tests

| ID | Test case name | Description |
|----|----------------|-------------|
| P01 | `crc32_empty_input`                      | CRC-32 of empty span equals 0x00000000. |
| P02 | `crc32_known_value`                      | CRC-32 of `{0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39}` ("123456789") equals 0xCBF43926. |
| P03 | `crash_safe_write_then_read`             | Write a payload; `read_latest()` returns the same payload. |
| P04 | `crash_safe_corrupted_dat_falls_back`    | Corrupt `.dat` (flip a byte); `read_latest()` returns the payload from `.bak`. |
| P05 | `crash_safe_no_files_returns_nullopt`    | `read_latest()` on an empty directory returns `std::nullopt`. |
| P06 | `crash_safe_tmp_used_when_only_file`     | Only `.tmp` with a valid checksum is present; `read_latest()` returns its payload. |
| P07 | `crash_safe_remove_all_deletes_files`    | After `remove_all()` no `.dat`, `.bak`, or `.tmp` file remains. |
| P08 | `session_persistence_round_trip`         | Save a `SessionState` with subscriptions; `load_all()` returns an equal vector. |
| P09 | `session_persistence_multiple_sessions`  | Save several sessions; `load_all()` returns them all in order. |
| P10 | `session_persistence_empty_on_no_file`   | `load_all()` returns empty vector when no snapshot exists. |
| P11 | `session_subscription_with_identifier`   | Subscription identifier is preserved through round-trip. |
| P12 | `retained_persistence_round_trip`        | Save a `Message` with properties; `load_all()` returns equal vector. |
| P13 | `retained_persistence_empty_payload`     | Message with empty payload round-trips correctly. |
| P14 | `retained_persistence_all_property_types` | Message with one property of each variant type round-trips. |
| P15 | `inflight_persistence_round_trip`        | Save `ClientEntry` records; `load_all()` returns equal client_id and entry fields. |
| P16 | `inflight_persistence_timestamp_reset`   | Loaded timestamp is ≥ timestamp captured just before `load_all()`. |
| P17 | `inflight_persistence_empty_on_no_file`  | `load_all()` returns empty vector when no snapshot exists. |
| P18 | `crash_safe_overwrite_sequence`          | Three successive writes; `read_latest()` always returns the latest data. |
| P19 | `crash_safe_directory_dat_falls_back_to_bak` | `.dat` path exists as a directory; reader skips it and loads valid `.bak`. |
| P20 | `crash_safe_remove_all_throws_on_nonempty_directory` | `remove_all()` throws `WriteFailure` when managed path cannot be deleted (non-empty directory). |
| P21 | `offline_queue_persistence_empty_on_no_file` | `load_all()` returns empty vector when no snapshot exists. |
| P22 | `offline_queue_persistence_round_trip` | Save one client with two messages; `load_all()` returns equal data. |
| P23 | `offline_queue_persistence_multiple_clients` | Save two clients; `load_all()` returns both with correct messages. |
| P24 | `offline_queue_persistence_fifo_order` | Messages within a client preserve FIFO order across save/load. |
| P25 | `offline_queue_persistence_all_property_types` | Message with one property of each variant type round-trips correctly. |
| P26 | `offline_queue_snapshot_empty` | `OfflineQueue::snapshot()` returns empty map when no messages enqueued. |
| P27 | `offline_queue_snapshot_single_client` | `snapshot()` returns correct messages for one client. |
| P28 | `offline_queue_snapshot_skips_empty_queues` | Clients whose queues were drained are omitted from snapshot. |
| P29 | `offline_queue_restore_replaces_queue` | `restore()` replaces any existing queue; `drain()` returns the restored messages. |
| P30 | `offline_queue_restore_fifo_order` | `drain()` after `restore()` returns messages in the same FIFO order they were passed to `restore()`. |
| P31 | `offline_queue_restore_timestamp_fresh` | `enqueue_time` of restored messages is ≥ the timestamp captured just before `restore()`. |
