# automation_client test specification

## Scope

Unit tests for automation client runtime rule synchronization behavior and INI mapping.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `automation_component_run_loads_rules_from_filestore` | startup rule load over FileStore HTTP | mock file store returns root with one rule | run loads one rule and hasRule true |
| `automation_component_monitoring_event_reload_rules` | monitor event triggers reload when keyPath matches | changed payload on `$MONITOR/FileStore/changed` with keyPath `/automation/rules` and updated file store payload | in-memory rules are reloaded |
| `automation_component_management_update_persists_and_acks` | runtime rule set command path | topic `$MONITORING/automation/rules/<name>/set` with JSON payload | rule updated, full tree posted to file store, ack published |
| `load_automation_client_runtime_config_from_ini` | config mapping for filestore and automation fields | ini with mqtt, filestore, monitoring and automation entries | parsed config values match inputs |
