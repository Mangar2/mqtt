# value_service test specification

## Scope

Unit tests for ValueService phase 2 component behavior.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `value_service_run_loads_values_and_publishes_replay` | startup load from FileStore and retained replay publish | FileStore GET returns map with two values | component loads both and publishes two retained messages |
| `value_service_set_updates_map_publishes_and_persists` | `/set` command path with valid value | inbound `house/light/set` with string value | key `house/light` updated, retained publish emitted, full map posted to FileStore |
| `value_service_monitoring_reload_replaces_values_and_replays` | monitor payload triggers reload by keyPath | monitor message with matching keyPath and changed FileStore payload | map is replaced and replay publishes new keys |
| `value_service_rejects_non_integral_numeric_set_value` | invalid numeric value type for `/set` | inbound `house/temp/set` with value `21.5` | no publish, no FileStore POST, map unchanged |
| `value_service_get_subscriptions_contains_monitor_and_set_topics` | subscription projection | loaded map with two keys | subscriptions contain monitor `/#` and both `<key>/set` topics |
