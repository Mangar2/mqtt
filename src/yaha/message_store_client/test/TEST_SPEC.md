# message_store_client test specification

## Scope

Unit tests for MessageStore runtime config parsing.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `load_config_parses_mqtt_server_persist_and_subscriptions` | Full config parsing | ini file with all sections including repeated `[subscription]` blocks | parsed runtime config matches file values |
| `load_config_uses_default_subscription_when_missing` | subscription default path | ini file without `[subscription]` | map contains only `#` with QoS 1 |
| `load_config_rejects_invalid_subscription_qos` | invalid QoS value handling | subscription with qos `9` | parser fails and returns error |
| `load_config_rejects_legacy_subscriptions_section` | no compatibility mode for legacy subscription map section | ini file using `[subscriptions]` | parser fails with migration hint |
| `load_config_rejects_invalid_numeric_fields` | numeric range validation | invalid `mqtt.port` | parser fails and returns error |
| `load_config_parses_tree_compression_parameters` | tree compression tuning parsing | ini file with all `[tree]` keys | parsed tree config values match file values |
| `load_config_rejects_invalid_tree_factor_values` | tree factor validation | `[tree]` with non-numeric `upperBoundFactor` | parser fails and returns error |
