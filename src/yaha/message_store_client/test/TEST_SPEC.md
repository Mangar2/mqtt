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
| `load_config_accepts_zero_length_for_further_compression` | preserve documented zero-value behavior | `[tree]` with `lengthForFurtherCompression = 0` | parser succeeds and keeps value `0` |
| `load_config_rejects_invalid_tree_factor_values` | tree factor validation | `[tree]` with non-numeric `upperBoundFactor` | parser fails and returns error |
| `load_config_rejects_out_of_range_tree_factor_values` | tree factor range guard | `[tree]` with `upperBoundFactor = 1001` | parser fails with range error |
| `load_config_rejects_subscription_unknown_key` | strict subscription key validation | `[subscription]` with unsupported key | parser fails with unknown-key error |
| `load_config_rejects_subscription_qos_without_topic` | enforce topic-before-qos contract | `[subscription]` starts with `qos` | parser fails with ordering error |
| `load_config_rejects_subscription_topic_without_qos` | enforce topic/qos pair completeness | `[subscription]` has `topic` only | parser fails with missing-qos error |
| `load_config_rejects_empty_subscription_topic` | reject empty topic values | `[subscription]` with empty `topic` and valid `qos` | parser fails with empty-topic error |
| `load_config_parses_server_host_when_set` | optional server host parsing | `[server]` with explicit `host` | parsed server host matches input |
| `load_config_parses_log_incoming_messages_when_enabled` | optional incoming-message log flag parsing | `[messagestore]` with `logIncomingMessages = true` | parser enables runtime incoming log flag |
| `load_config_rejects_invalid_log_incoming_messages_value` | strict bool validation for incoming-message log flag | `[messagestore]` with non-bool `logIncomingMessages` | parser fails with key-specific bool error |
| `load_config_defaults_log_reason_to_enabled` | reason logging should be enabled when not configured | minimal valid config without `messagestore.logReason` | runtime keeps reason logging enabled |
| `load_config_parses_log_reason_when_disabled` | optional reason logging flag parsing | `[messagestore]` with `logReason = false` | runtime disables reason logging for inbound logs and mqtt message trace |
| `load_config_rejects_invalid_log_reason_value` | strict bool validation for reason log flag | `[messagestore]` with non-bool `logReason` | parser fails with key-specific bool error |
