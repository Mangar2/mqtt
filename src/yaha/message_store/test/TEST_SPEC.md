# message_store/message_tree test specification

## Scope

Unit tests for MessageTree behavior required by step 4.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `add_data_creates_node_and_get_section_returns_it` | First insert creates node | one message, query root depth 3 | one node with topic/value |
| `add_data_updates_move_previous_value_into_history` | Updating topic records previous state | two messages same topic | current value is second, history has first |
| `add_data_prefers_first_reason_timestamp_when_valid_iso` | Node timestamp should use first reason timestamp when parseable | message with reason[0].timestamp ISO string and divergent clock | node timeMs equals parsed reason timestamp |
| `add_data_falls_back_to_clock_when_first_reason_timestamp_invalid` | Invalid first reason timestamp must not override clock | message with invalid reason[0].timestamp and valid older reason entry | node timeMs equals injected clock time |
| `add_data_parses_reason_timestamp_with_positive_timezone_offset` | Positive timezone offset must be converted to UTC milliseconds | reason[0].timestamp with `+HH:MM` offset | node timeMs equals expected UTC epoch milliseconds |
| `add_data_parses_reason_timestamp_with_negative_offset_and_fraction` | Negative timezone offset and fractional seconds must parse | reason[0].timestamp with fraction and `-HH:MM` offset | node timeMs equals expected UTC epoch milliseconds |
| `add_data_falls_back_to_clock_for_invalid_reason_timezone_format` | Unsupported timezone format must fallback | reason[0].timestamp without colon in offset | node timeMs equals injected clock time |
| `add_data_falls_back_to_clock_for_invalid_reason_fraction_format` | Malformed fractional part must fallback | reason[0].timestamp with empty fraction | node timeMs equals injected clock time |
| `history_is_trimmed_with_hysteresis` | Bounded history applies batch trim | max=3 hysterese=1 with repeated updates | history size <= 3 and not empty |
| `history_compresses_repeated_equal_values` | Repeated equal updates use compressed buckets | maxValuesPerHistoryEntry=2 with repeated same value | decompressed history shows merged timestamps for compressed repeats |
| `history_single_compression_keeps_reasoned_entries_separate` | Single compression path keeps non-groupable entries separate | same topic with different values and different reason messages | history has separate entries with their own reasons |
| `history_time_value_compression_merges_value_sequence` | TimeValue compression groups same-reason value sequence | same topic with changing values and same reason message | history shows grouped sequence with reason only on oldest entry |
| `history_time_compression_merges_identical_values_without_interval` | Time compression groups identical values when interval conversion does not match | same topic with identical values, same reason, irregular gaps | history expands to multiple identical values with reason only on oldest entry |
| `history_interval_compression_merges_regular_updates` | Interval compression groups regular identical-value updates | same topic with identical values and regular timestamps | history has one entry with `regular update, amount: N` marker |
| `history_single_entry_preserves_reason` | First historic state is represented as single entry | two updates same topic | one history entry with original value and reason |
| `history_time_value_entry_keeps_oldest_reason_only` | TimeValue decompression keeps reason only on oldest element of compressed block | updates with same reason messages but changing values | history entries are newest-first and only oldest entry in block contains reason |
| `history_time_entry_for_identical_values_same_reason` | Time compression stores repeated identical values with shared reason chain | repeated updates with same value and same reason messages | decompressed history contains multiple entries with identical value and oldest entry keeps reason |
| `history_interval_entry_for_regular_updates` | Interval compression is used for regular updates with matching bounds | repeated equal-value updates with fixed step and low `lengthForFurtherCompression` | decompressed history contains synthetic `regular update, amount: N` reason entry |
| `history_interval_entry_rejects_irregular_updates` | Irregular timing does not stay in interval compression | repeated equal-value updates with one interval outside bounds | decompressed history keeps expanded non-interval entries |
| `history_single_entries_do_not_duplicate_timestamps` | No duplication in single-entry path | strictly increasing timestamps with alternating values | all history timestamps are unique |
| `history_time_value_entries_do_not_duplicate_timestamps` | No duplication in timeValue path | strictly increasing timestamps with varying values and equal reason message | all history timestamps are unique |
| `history_time_value_reason_timestamp_override_adds_one_history_entry_per_message` | Each single update must add exactly one decompressed history entry even if reason timestamps are identical | sequential updates with same reason message and same parseable reason timestamp | after update N, decompressed history size is `N-1` |
| `history_time_entries_do_not_duplicate_timestamps` | No duplication in time path | strictly increasing timestamps with identical value and irregular spacing | all history timestamps are unique |
| `history_interval_entries_do_not_duplicate_timestamps` | No duplication in interval path | strictly increasing timestamps with identical value and regular spacing over many updates | all history timestamps are unique |
| `history_short_regular_tail_keeps_latest_visible_update` | Time-to-interval split must not trigger too early | timestamps `0, 1000, 5000, 9000` with `lengthForFurtherCompression=3` | history still contains timestamp `5000` |
| `history_interval_entry_reports_latest_previous_timestamp` | Interval decompression must expose newest historic timestamp | regular updates with identical values and `lengthForFurtherCompression=3` | history head timestamp equals latest previous sample |
| `history_is_returned_newest_first` | Decompressed history order matches original newest-first behavior | multiple updates with distinct timestamps | `history[0]` is the newest history item |
| `history_grouping_compares_reason_messages_only` | Compression grouping ignores reason timestamps and compares reason messages only | updates with same reason messages but different reason timestamps | entries are grouped into compressed blocks instead of split by timestamp-only differences |
| `get_section_respects_depth` | Depth-limited query | multi-level topics, depth 0 and 1 | deeper nodes excluded for smaller depth |
| `get_section_can_exclude_reason_and_history` | Projection flags are honored | includeReason=false includeHistory=false | reason/history empty in result |
| `get_section_excludes_node_reason_but_keeps_history_reasons` | Legacy projection behavior for history reasons | includeReason=false includeHistory=true after one update | node reason empty but history reasons preserved |
| `get_nodes_returns_only_changed_or_new_nodes` | Snapshot diff mode | snapshot with same and different topics | only changed/new topics returned |
| `cleanup_removes_stale_nodes_and_prunes_empty_branches` | Old nodes removed by day cutoff | old + fresh nodes, cleanup(1) | stale removed, fresh kept |
| `wildcard_filter_matching_not_used_in_tree_queries` | Prefix query is structural, no wildcard semantics | stored topic with plus/hash chars | exact path behavior retained |
| `persist_now_writes_snapshot_and_restore_latest_rebuilds_tree` | Round-trip persistence | tree with multiple topics/history | restored tree equals persisted state |
| `restore_latest_skips_corrupt_newest_file_and_uses_previous_valid` | Most recent valid file must be selected | valid file + newer corrupt file | restore succeeds from older valid file |
| `restore_latest_returns_false_when_no_files_exist` | No snapshot available | empty directory | restore returns false |
| `restore_latest_skips_malformed_node_payload` | Valid header with malformed node data | file with MTREE1 and invalid node body | restore returns false |
| `retention_deletes_old_files_beyond_keep_files` | Snapshot retention enforcement | keepFiles=2 with 3 persists | only newest two files remain |
| `retention_keep_files_zero_disables_deletion` | Retention disabled branch | keepFiles=0 with multiple persists | all files remain |
| `persist_now_returns_false_when_directory_is_regular_file` | create_directories failure path | directory path points to regular file | persistNow returns false |
| `start_periodic_persists_until_stopped` | Periodic persistence loop | short interval + running period | at least one snapshot file created |
| `start_periodic_noop_when_interval_zero_or_already_running` | startPeriodic guard branches | interval=0 and repeated start call | no periodic files for interval=0 and stable run for repeated start |
| `default_constructor_can_persist_and_restore_reason_history` | Default-config constructor and reason/history serialization | value + reason + history | roundtrip keeps reason and history entries |
| `get_subscriptions_returns_configured_map` | MessageStore forwards configured subscriptions | config map with multiple entries | returned map equals config map |
| `handle_message_adds_regular_topic_to_tree` | Non-cleanup message must be stored | regular topic/value message | querySection contains node |
| `handle_message_cleanup_topic_uses_numeric_payload` | Cleanup dispatch with numeric payload | old node + cleanup payload 1 | stale node removed |
| `handle_message_cleanup_topic_ignores_invalid_payload` | Invalid cleanup payload must be ignored | existing node + cleanup payload text | existing node remains |
| `run_restore_starts_callbacks_and_periodic_persist` | Lifecycle run path | pre-existing snapshot + callbacks + interval>0 | restored data present, start/stop callbacks invoked, periodic file created |
| `close_performs_final_persist_when_periodic_disabled` | Final persist on shutdown | interval=0 run/close | at least one snapshot file exists |
| `run_and_close_are_idempotent` | Repeated lifecycle calls are safe | run twice and close twice | callbacks invoked once each |
| `http_get_store_returns_json_for_topic_prefix` | GET endpoint section mode with defaults | GET `/store/home` | status 200 and JSON array with matching topics |
| `http_get_store_decodes_percent_encoded_topic_prefix` | Topic path is percent-decoded | GET `/store/home%20room` | status 200 and topics under `home room` returned |
| `http_get_store_rejects_invalid_percent_encoded_topic_prefix` | Invalid percent-encoding path handling | GET `/store/home%2` | status 400 with YahaError payload code `YAHA_MESSAGE_STORE_HTTP_INVALID_PERCENT_ENCODING` |
| `http_get_store_decodes_percent_encoded_hex_bytes` | Percent decoder hex branch coverage | GET `/store/home%41room` with matching topic | status 200 and decoded topic prefix is resolved |
| `http_get_store_applies_levelamount_history_reason_headers` | Header options are honored | GET with `levelamount=2`, `history=true`, `reason=false` | status 200 and returned nodes include history but no reason |
| `http_get_store_header_defaults_apply_for_blank_or_invalid_values` | Header parser fallback branches | GET with blank `levelamount/history` and invalid `reason` | status 200 and request is processed with defaults |
| `http_get_store_snapshot_body_uses_diff_mode` | JSON body activates diff mode | GET body with prior snapshot array | only changed/new topics returned |
| `http_get_store_snapshot_body_skips_unknown_fields_and_json_variants` | Snapshot parser unknown-field skipping | GET body with unknown object/array/boolean/null fields | status 200 and parser keeps valid topic/value entries |
| `http_get_store_snapshot_body_supports_escaped_strings` | Snapshot parser escaped-string handling | GET body with escaped slash/quote/newline/tab/backslash sequences | status 200 and parser accepts escaped string payloads |
| `http_get_store_snapshot_body_detects_time_only_updates` | Snapshot diff must treat newer node timestamp as change even when topic/value remain equal | GET snapshot body includes prior `time` with same topic/value, then one new identical-value message arrives | status 200 and response contains updated topic |
| `http_post_store_sensor_payload_uses_topic_and_query_flags` | sensor.php-compatible POST fields drive section query | POST body with `topic`, `history`, `reason`, `levelAmount` | status 200 and section query applies topic + flags |
| `http_post_store_sensor_payload_nodes_activates_diff_mode` | sensor.php-compatible `nodes` field drives diff query | POST body with `nodes` snapshot array | status 200 and only changed/new nodes returned |
| `http_post_store_sensor_payload_empty_nodes_uses_section_query` | sensor.php-compatible empty `nodes` must not force diff mode | POST body with `topic`, flags and `nodes: []` | status 200 and section query uses topic/history/reason filters |
| `http_post_store_sensor_payload_accepts_json_boolean_flags` | sensor.php-compatible flags accept JSON booleans and legacy lower-case level key | POST body with `history: true`, `reason: false`, `levelamount` and `nodes: []` | status 200 and section query honors flags and level |
| `http_post_store_invalid_json_falls_back_to_section_query` | Invalid sensor POST body keeps legacy fallback behavior | POST body `not-json` | status 200 and section query defaults are used |
| `http_get_store_malformed_snapshot_returns_empty_array` | Malformed body fallback path | GET body `not-json` | status 200 and `[]` |
| `http_unknown_path_returns_404` | Non-store endpoint is rejected | GET `/unknown/path` | status 404 with YahaError payload code `YAHA_MESSAGE_STORE_HTTP_NOT_FOUND` |
| `http_get_store_json_output_escapes_special_characters` | JSON escaping branch coverage | store string payload with quote/backslash/newline/carriage-return/tab | response payload contains escaped JSON control sequences |
| `http_get_store_outputs_iso_time_and_reason_timestamps` | HTTP response should expose ISO UTC time fields and preserve reason timestamps | two updates with explicit reason timestamps and history enabled | response uses `time` ISO strings (including history), contains reason timestamps, and contains no `timeMs` field |
| `handle_message_cleanup_topic_accepts_numeric_string_payload` | Cleanup path string-number conversion | cleanup message with payload "1" | stale nodes are removed |
| `iso_parser_accepts_leap_day_and_roundtrips` | Leap-year parsing branch is valid | `2024-02-29T12:34:56Z` | parse succeeds and roundtrip emits canonical ISO with milliseconds |
| `iso_parser_rejects_non_leap_february_29` | Day range validation for non-leap year | `2023-02-29T00:00:00Z` | parse returns false |
| `iso_parser_rejects_invalid_month_and_day_combinations` | Month/day guards reject impossible dates | `2024-13-01T00:00:00Z` and `2024-04-31T00:00:00Z` | parse returns false |
| `iso_parser_rejects_invalid_timezone_ranges` | Timezone bound validation | `+24:00` and `+01:60` offsets | parse returns false |
| `iso_parser_rejects_trailing_characters` | Strict end-of-input validation | valid timestamp plus trailing text | parse returns false |
| `iso_formatter_handles_negative_milliseconds` | Negative epoch formatting normalization | `-1` milliseconds | output equals `1969-12-31T23:59:59.999Z` |
