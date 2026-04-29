# message_store/message_tree test specification

## Scope

Unit tests for MessageTree behavior required by step 4.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `add_data_creates_node_and_get_section_returns_it` | First insert creates node | one message, query root depth 3 | one node with topic/value |
| `add_data_updates_move_previous_value_into_history` | Updating topic records previous state | two messages same topic | current value is second, history has first |
| `history_is_trimmed_with_hysteresis` | Bounded history applies batch trim | max=3 hysterese=1 with repeated updates | history size <= 3 and not empty |
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
