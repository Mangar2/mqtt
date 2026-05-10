# remote_service tests

## Scope

Unit tests for RemoteService phase 1 runtime config mapping and validation.

## Planned test cases

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `remote_service_runtime_config_parses_all_sections` | full runtime mapping for standalone composition | ini with mqtt, filestore and remoteservice sections | parsed runtime config contains mapped values including `filename -> mappingKeyPath` and `topicPrefix -> monitorTopicPrefix` |
| `remote_service_runtime_config_rejects_missing_mapping_key_path` | required mapping key path missing | ini without `filestore.filename` | load fails with field-specific missing-setting error |
| `remote_service_runtime_config_rejects_missing_filestore_host` | required endpoint host missing | ini without `filestore.host` | load fails with field-specific missing-setting error |
| `remote_service_runtime_config_rejects_invalid_subscribe_qos` | invalid QoS parser path | ini with `remoteservice.subscribeQoS=7` | load fails with field-specific range error |
| `remote_service_runtime_config_rejects_invalid_filestore_port` | invalid port parser path | ini with `filestore.port=70000` | load fails with field-specific range error |