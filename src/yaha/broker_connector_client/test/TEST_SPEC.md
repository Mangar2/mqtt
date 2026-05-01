# broker_connector_client test specification

## Scope

Unit tests for runtime orchestration and INI config mapping of the standalone broker connector client.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `load_runtime_config_parses_source_receiver_and_automation` | Full config parsing for phase 4 composition | ini document with source, subscriptions, receiver, automation, monitoring sections | runtime config contains all mapped values and boolean overrides |
| `load_runtime_config_uses_defaults_when_optional_keys_missing` | Default-preserving path | ini document containing only required host/port keys | missing optional values stay at struct defaults and source subscriptions default to `#` qos1 |
| `load_runtime_config_rejects_invalid_bool_field` | bool parser error path | ini document with `automation.retainPassthrough=maybe` | parser returns false and error mentions invalid boolean field |
| `broker_connector_client_config_applies_keepalive_fallback_and_monitoring_trace` | Fallback and optional monitoring branches | config without automation.sourceKeepAliveIntervalMs and with monitoring.sourceLifecycleTrace | source lifecycle keepalive falls back to source keepAliveSeconds and monitoring trace flag is applied |
| `runtime_start_and_close_follow_required_order` | deterministic start/stop choreography | fake receiver/source/component runtime ports with order recording | start order receiver->connector->source and close order source->receiver->connector |
| `runtime_start_propagates_receiver_start_failure` | startup failure propagation path | fake receiver start returns false with error text | runtime start returns false with same error and no source/component start |
