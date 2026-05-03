# file_store_client tests

## Scope

Unit tests for `FileStoreClientRuntimeConfig` INI mapping behavior.

## Planned tests

| Test name | Scenario | Input | Expected |
|---|---|---|---|
| `load_config_parses_mqtt_filestore_and_monitoring_sections` | Happy path mapping | valid INI with mqtt+filestore+monitoring | parsed values match file |
| `load_config_uses_defaults_when_sections_missing` | Default behavior | minimal INI | defaults remain active |
| `load_config_rejects_invalid_monitoring_qos` | Validation of range-bound numeric field | qos=9 | parser fails with error |
| `load_config_rejects_invalid_bool_field` | Validation of bool parsing | retain=maybe | parser fails with error |
| `load_config_rejects_invalid_server_port` | Validation of server port range parsing | server port non-numeric | parser fails with error |
| `load_config_rejects_invalid_watch_interval` | Validation of watch interval range parsing | watchIntervalMs out of range | parser fails with error |
| `load_runtime_config_rejects_invalid_mqtt_port` | Runtime config fails when mqtt parser fails | mqtt port non-numeric with valid filestore section | parser fails with mqtt error |
