# remote_service tests

## Scope

Unit tests for RemoteService phase 1 and phase 2:

- runtime config mapping and validation
- mapping payload parser and duplicate-path behavior
- startup mapping load and monitor-triggered reload lifecycle
- command resolution and publish callback handoff

## Planned test cases

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `remote_service_runtime_config_parses_all_sections` | full runtime mapping for standalone composition | ini with mqtt, filestore and remoteservice sections | parsed runtime config contains mapped values including `filename -> mappingKeyPath` and `topicPrefix -> monitorTopicPrefix` |
| `remote_service_runtime_config_rejects_missing_mapping_key_path` | required mapping key path missing | ini without `filestore.filename` | load fails with field-specific missing-setting error |
| `remote_service_runtime_config_rejects_missing_filestore_host` | required endpoint host missing | ini without `filestore.host` | load fails with field-specific missing-setting error |
| `remote_service_runtime_config_rejects_invalid_subscribe_qos` | invalid QoS parser path | ini with `remoteservice.subscribeQoS=7` | load fails with field-specific range error |
| `remote_service_runtime_config_rejects_invalid_filestore_port` | invalid port parser path | ini with `filestore.port=70000` | load fails with field-specific range error |
| `remote_service_mapping_payload_parser_accepts_valid_services` | valid FileStore payload parser path | payload with one service entry including optional fields | parser succeeds and produces expected map entries |
| `remote_service_mapping_payload_parser_rejects_invalid_payload_without_partial_apply` | all-or-nothing parser behavior | payload with one valid and one invalid service entry | parser fails and output map remains unchanged |
| `remote_service_mapping_payload_parser_duplicate_path_keeps_first_and_writes_cerr` | duplicate-path compatibility rule | payload with duplicate service paths | parser succeeds, first entry is kept, duplicate emits `std::cerr` message |
| `remote_service_monitor_key_path_parser_extracts_key_path` | monitor payload helper | monitor JSON with string `keyPath` | helper returns matching key path |
| `remote_service_component_run_loads_mapping_from_filestore` | startup mapping load | component run with valid FileStore GET payload | component stores parsed mapping |
| `remote_service_component_run_keeps_empty_map_on_startup_load_failure` | startup failure fallback | component run with FileStore HTTP error | component stays running with empty map |
| `remote_service_component_monitor_matching_key_path_reloads_mapping` | monitor-triggered reload | matching monitor event and changed FileStore payload | mapping is replaced atomically with new payload |
| `remote_service_component_monitor_invalid_reload_payload_keeps_previous_map` | invalid reload fallback behavior | matching monitor event with invalid FileStore payload | previous valid mapping remains active |
| `remote_service_component_resolve_command_builds_outbound_message` | domain command resolution success | loaded mapping and request with path/device/state/token | result is success and contains mapped outbound message with topic/value/qos/reason |
| `remote_service_component_resolve_command_returns_service_not_found_for_unknown_device` | domain resolution not-found path | loaded mapping with unknown device id | result is `ServiceNotFound` and has no resolved message |
| `remote_service_component_publish_command_calls_callback_with_resolved_message` | publish callback handoff success | loaded mapping, valid request, callback collecting messages | result is success and callback gets mapped message |
| `remote_service_component_publish_command_returns_publish_failed_when_callback_missing` | publish callback missing path | loaded mapping, valid request, no callback set | result is `PublishFailed` with resolved message preserved |
| `remote_service_component_publish_command_returns_publish_failed_when_callback_throws` | publish callback failure translation | loaded mapping, valid request, callback throwing exception | result is `PublishFailed` with resolved message preserved |