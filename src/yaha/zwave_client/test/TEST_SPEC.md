# TEST_SPEC.md — yaha/zwave_client

All tests are tagged `[zwave_client]`.

## zwave_client_app_test.cpp — runtime config schema and integration mapping

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `load_zwave_config_applies_defaults_and_parses_device` | Unit validation for defaults and device row parsing | minimal valid `[zwave]` section with one `device` row | parse success; default qos/retain values preserved; required usb/topic loaded; device row parsed |
| `load_zwave_config_rejects_invalid_device_row` | Unit validation for schema bounds | `zwave.device` row with invalid node id | parse fails with deterministic error containing `nodeId` range text |
| `load_zwave_config_rejects_invalid_device_field_count` | Device entry must include topic and node id | `zwave.device` row with only topic field | parse fails with deterministic field-count error |
| `load_zwave_config_rejects_empty_device_topic` | Device topic must be non-empty | `zwave.device` row with empty topic token | parse fails with deterministic topic-empty error |
| `load_zwave_config_rejects_invalid_optional_numeric_fields` | Optional class/instance/index bounds and parsing | three configs with invalid classId/instance/index values | parse fails with deterministic field-specific error text |
| `load_zwave_config_rejects_invalid_qos_and_retain_values` | QoS/retain parser validation | invalid `subscribeQoS`, invalid `qos`, invalid `retain` | parse fails and error references corresponding key |
| `load_zwave_config_parses_log_message_flags` | Optional logging bool flags are mapped from ini | valid `[zwave]` with `logIncomingMessages=true` and `logOutgoingMessages=true` | parse success and both logging flags enabled in config |
| `load_zwave_config_keeps_message_flags_when_log_level_is_set` | `logLevel` does not override message-trace flags | `[zwave]` with `logLevel=3` and explicit true message-log flags | parse success; log level is `3`; incoming/outgoing message logs remain enabled |
| `load_zwave_config_keeps_message_flags_for_log_level_zero` | `logLevel=0` still keeps message-trace flags independent | `[zwave]` with `logLevel=0` and explicit true message-log flags | parse success; log level is `0`; incoming/outgoing message logs remain enabled |
| `load_zwave_config_rejects_invalid_log_level` | Unified log level must stay inside allowed range | `[zwave]` with out-of-range `logLevel` | parse fails and error references `zwave.logLevel` |
| `load_zwave_config_rejects_invalid_log_outgoing_messages_value` | strict bool validation for outgoing message logging flag | `[zwave]` with non-bool `logOutgoingMessages` | parser fails and error references `zwave.logOutgoingMessages` |
| `load_zwave_config_parses_poll_interval_ms_and_rejects_out_of_range` | Poll interval setting is parsed and bounded | valid `pollIntervalMs=750` and invalid `pollIntervalMs=0` | valid config sets `pollIntervalMs`; invalid config fails and references `zwave.pollIntervalMs` |
| `load_zwave_config_parses_command_reaction_timing_and_rejects_out_of_range` | Command reaction poll/timeout settings are parsed and bounded | valid `commandReactionPollIntervalMs=200` and `commandReactionTimeoutMs=30000`, invalid `commandReactionTimeoutMs=0` | valid config sets both fields; invalid config fails and references `zwave.commandReactionTimeoutMs` |
| `load_zwave_config_requires_usb_settings` | Mandatory USB settings | configs missing `usbDevice` or missing `usbTopic` | parse fails with missing-setting error |
| `load_zwave_runtime_config_combines_zwave_and_mqtt_sections` | Runtime integration mapping of combined config | valid `[zwave]` and `[mqtt]` fields | parse success; zwave and mqtt outputs reflect configured values |
| `load_zwave_config_parses_filestore_settings` | Filestore section is parsed for settings sync | `[filestore]` with `use`, `host`, `port`, `filename` | parse success; config stores filestore enablement and endpoint/key-path values |
| `load_zwave_config_parses_filestore_monitor_topic_prefix` | Filestore monitor topic prefix is parsed for runtime reload trigger subscription | `[filestore]` with `topicPrefix` plus valid zwave section | parse success; config stores `fileStoreMonitorTopicPrefix` |
| `load_zwave_config_rejects_invalid_filestore_retry_interval` | Filestore startup retry interval range validation | `[filestore]` with `startupRetryIntervalSeconds=0` and valid zwave section | parse fails and error references `filestore.startupRetryIntervalSeconds` |
| `apply_zwave_device_settings_from_json_overrides_ini_nodes_completely` | Any filestore setting for a node replaces all INI settings for that node | ini config with multiple rows for one node plus filestore JSON `devices` with same node | resulting config removes all INI rows for that node and keeps only filestore rows for that node |
| `apply_zwave_device_settings_from_json_rejects_unknown_root_keys` | Filestore JSON root is strict and devices-only | JSON containing `devices` plus additional root key | parse fails with deterministic unknown-root-key error |
| `serialize_zwave_settings_to_json_writes_only_devices_root` | Filestore persistence writes only device mappings | config populated with many non-device fields plus one device | serialized JSON contains only root `devices` and no non-device fields |
| `load_zwave_config_parses_legacy_json_equivalent_device_rows` | Legacy migration compatibility for topics with spaces and class rows without explicit type | `[zwave]` with topic containing spaces and `classId=38` row | parse success; topic text is preserved and class id/instance are mapped correctly |
| `load_zwave_runtime_config_reports_mqtt_validation_error` | Runtime integration error propagation | invalid mqtt port value | parse fails and reports mqtt range validation error |
| `load_zwave_config_allows_missing_device_setting` | Unit validation for optional device list | `[zwave]` without `device` key | parse succeeds and device list remains empty |
| `sync_zwave_settings_from_filestore_replaces_snapshot_and_persists` | FileStore startup sync helper applies full snapshot and persists normalized payload | enabled filestore config with mock GET payload and additional preloaded in-memory rows | helper returns true, config devices are replaced by FileStore snapshot, POST is emitted |
| `sync_zwave_settings_from_filestore_returns_true_when_disabled` | FileStore startup sync helper is a no-op when disabled | filestore.use=false with pre-populated devices | helper returns true and config remains unchanged |
| `sync_zwave_settings_from_filestore_reports_load_failure` | FileStore startup sync helper reports GET failure | enabled filestore config with mock GET status 500 | helper returns false and error text is set |
| `load_zwave_device_settings_snapshot_from_filestore_handles_deleted_key` | Runtime snapshot loader maps deleted key (404) to empty effective device set | enabled filestore config with mock GET status 404 and preloaded in-memory devices | helper returns true with empty output device list |
