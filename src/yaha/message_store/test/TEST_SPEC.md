# message_store/message_tree test specification

## Scope

Unit tests for MessageTree behavior required by step 4.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `add_data_creates_node_and_get_section_returns_it` | First insert creates node | one message, query root depth 3 | one node with topic/value |
| `add_data_updates_move_previous_value_into_history` | Updating topic records previous state | two messages same topic | current value is second, history has first |
| `history_is_trimmed_with_hysteresis` | Bounded history applies batch trim | max=3 hysterese=1 with repeated updates | history size <= 3 and not empty |
| `history_compresses_repeated_equal_values` | Repeated equal updates use compressed buckets | maxValuesPerHistoryEntry=2 with repeated same value | decompressed history shows merged timestamps for compressed repeats |
| `get_section_respects_depth` | Depth-limited query | multi-level topics, depth 0 and 1 | deeper nodes excluded for smaller depth |
| `get_section_can_exclude_reason_and_history` | Projection flags are honored | includeReason=false includeHistory=false | reason/history empty in result |
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
| `http_get_store_malformed_snapshot_returns_empty_array` | Malformed body fallback path | GET body `not-json` | status 200 and `[]` |
| `http_unknown_path_returns_404` | Non-store endpoint is rejected | GET `/unknown/path` | status 404 with YahaError payload code `YAHA_MESSAGE_STORE_HTTP_NOT_FOUND` |
| `http_get_store_json_output_escapes_special_characters` | JSON escaping branch coverage | store string payload with quote/backslash/newline/carriage-return/tab | response payload contains escaped JSON control sequences |
| `handle_message_cleanup_topic_accepts_numeric_string_payload` | Cleanup path string-number conversion | cleanup message with payload "1" | stale nodes are removed |
