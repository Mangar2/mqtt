# message_store_client test specification

## Scope

Unit tests for MessageStore standalone composition and runtime config parsing.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `load_config_parses_mqtt_server_persist_and_subscriptions` | Full config parsing | ini file with all sections | parsed runtime config matches file values |
| `load_config_uses_default_subscription_when_missing` | subscription default path | ini file without `[subscriptions]` | map contains only `#` with QoS 1 |
| `load_config_rejects_invalid_subscription_qos` | invalid QoS value handling | subscription with qos `9` | parser fails and returns error |
| `load_config_rejects_invalid_numeric_fields` | numeric range validation | invalid `mqtt.port` | parser fails and returns error |
| `run_and_close_update_app_running_state` | lifecycle composition | runtime config with server port `0` | app can run/close and `isRunning()` toggles correctly |
