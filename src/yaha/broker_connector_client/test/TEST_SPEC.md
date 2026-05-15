# broker_connector_client test specification

## Scope

Unit tests for INI config mapping of the standalone broker connector client.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `load_runtime_config_parses_source_receiver_and_automation` | Full config parsing for phase 4 composition | ini document with source, repeated `subscription` sections, receiver, automation, monitoring sections | runtime config contains all mapped values and boolean overrides |
| `load_runtime_config_uses_defaults_when_optional_keys_missing` | Default-preserving path | ini document containing only required host/port keys | missing optional values stay at struct defaults and source subscriptions default to `#` qos1 |
| `load_runtime_config_rejects_invalid_bool_field` | bool parser error path | ini document with `automation.retainPassthrough=maybe` | parser returns false and error mentions invalid boolean field |
| `load_runtime_config_rejects_incomplete_subscription_entry` | Structured subscription validation failure | ini document with `subscription.topic` but without `subscription.qos` | parser returns false and reports incomplete subscription entry |
| `load_runtime_config_ignores_legacy_source_subscriptions_section` | Legacy subscription section is no longer supported | ini document with `[sourceSubscriptions]` only | parser succeeds and applies default structured subscription `#` qos1 |
| `broker_connector_client_config_applies_keepalive_fallback_and_monitoring_trace` | Fallback and optional monitoring branches | config without automation.sourceKeepAliveIntervalMs and with monitoring.sourceLifecycleTrace | source lifecycle keepalive falls back to source keepAliveSeconds and monitoring trace flag is applied |
| `broker_connector_client_config_rejects_invalid_source_port` | Source section numeric validation failure | config with `sourceHttpBroker.port` outside valid range | parser returns false and reports source port error |
| `broker_connector_client_config_rejects_invalid_monitoring_trace_boolean` | Monitoring section bool validation failure | config with `monitoring.sourceLifecycleTrace=maybe` | parser returns false and reports monitoring bool error |
| `load_runtime_config_rejects_invalid_monitoring_log_reason` | Monitoring section reason-log bool validation failure | config with `monitoring.logReason=maybe` | parser returns false and reports monitoring bool error |
| `broker_connector_client_config_rejects_invalid_source_optional_fields` | Source section optional field validation failures | configs with invalid `listenerPort`, `keepAliveSeconds`, and `clean` values | parser returns false and reports corresponding source field errors |
| `broker_connector_client_config_rejects_invalid_receiver_and_automation_fields` | Receiver/automation numeric and bool validation failures | configs with invalid receiver and automation values | parser returns false and reports corresponding receiver/automation field errors |
