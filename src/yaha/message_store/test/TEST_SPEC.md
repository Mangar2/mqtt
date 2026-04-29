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
