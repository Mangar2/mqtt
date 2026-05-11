# value_service test specification

## Scope

Unit tests for ValueService phase 2 component behavior.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `value_service_run_loads_values_and_publishes_replay` | startup load from FileStore and retained replay publish | FileStore GET returns map with two values | component loads both and publishes two retained messages |
| `value_service_set_updates_map_publishes_and_persists` | `/set` command path with valid value | inbound `house/light/set` with string value | key `house/light` updated, retained publish emitted, full map posted to FileStore |
| `value_service_set_publishes_when_persist_fails` | FileStore POST failure must not suppress outbound state publish | inbound `house/light/set` with mock POST response 500 | retained publish emitted, map updated, POST attempted |
| `value_service_monitoring_reload_replaces_values_and_replays` | monitor payload triggers reload by keyPath | monitor message with matching keyPath and changed FileStore payload | map is replaced and replay publishes new keys |
| `value_service_logs_incoming_and_outgoing_messages` | runtime logging for incoming and sent messages | one `/set` input with active publish callback | stdout contains one `value_service[in]` and one `value_service[out]` line |
| `value_service_monitoring_non_matching_keypath_does_not_reload` | monitor payload for different keyPath must not reload | monitor message with `keyPath=/other/path` after initial load | map remains unchanged and no additional replay publish happens |
| `value_service_rejects_non_integral_numeric_set_value` | invalid numeric value type for `/set` | inbound `house/temp/set` with value `21.5` | no publish, no FileStore POST, map unchanged |
| `value_service_get_subscriptions_contains_monitor_and_set_topics` | subscription projection | loaded map with two keys | subscriptions contain monitor `/#` and both `<key>/set` topics |
| `value_service_runtime_config_parses_all_sections` | full runtime mapping for standalone composition | ini with mqtt, filestore and valueservice sections | parsed runtime config contains mapped values from single-source filestore filename/topicPrefix and valueservice qos |
| `value_service_runtime_config_rejects_invalid_subscribe_qos` | invalid qos parser path | ini with `valueservice.subscribeQoS=9` | load fails with field-specific error |
| `value_service_runtime_config_rejects_invalid_filestore_use` | invalid bool parser path | ini with `filestore.use=maybe` | load fails with field-specific error |

## Additional assertions on existing tests

- `value_service_run_loads_values_and_publishes_replay`: replay messages include a startup reason entry.
- `value_service_monitoring_reload_replaces_values_and_replays`: replay messages after monitor-triggered reload include a file-change reason entry.
