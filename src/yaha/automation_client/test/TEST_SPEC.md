# automation_client test specification

## Scope

Unit tests for automation client runtime rule synchronization behavior and INI mapping.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `automation_component_run_loads_rules_from_filestore` | startup rule load over FileStore HTTP | mock file store returns root with one rule | run loads one rule and hasRule true |
| `automation_component_monitoring_event_reload_rules` | monitor event triggers reload when keyPath matches | changed payload on `$MONITOR/FileStore/changed` with keyPath `/automation/rules` and updated file store payload | in-memory rules are reloaded |
| `automation_component_management_update_persists_and_acks` | runtime rule set command path | topic `$MONITORING/automation/rules/<name>/set` with JSON payload | rule updated, full tree posted to file store, ack published |
| `automation_component_evaluates_rules_and_publishes_outputs` | domain message triggers full tree evaluation | loaded rule checks `$MONITORING/presence/set` and inbound domain message sets it to `on` | output message is published for matching rule |
| `load_automation_client_runtime_config_from_ini` | config mapping for filestore and automation fields | ini with mqtt, filestore, monitoring and automation entries | parsed config values match inputs |
| `load_automation_client_runtime_config_defaults_logging_flags_to_false` | defaults for optional logging switches | ini without automation logging flags | both logging flags in runtime config remain false |
| `load_automation_client_runtime_config_reports_invalid_log_incoming_messages` | invalid bool validation for incoming logging flag | ini with `automation.logIncomingMessages=maybe` | loader fails and reports field-specific error |
| `load_automation_client_runtime_config_reports_invalid_log_outgoing_messages` | invalid bool validation for outgoing logging flag | ini with `automation.logOutgoingMessages=maybe` | loader fails and reports field-specific error |
| `automation_component_logs_incoming_and_outgoing_messages_when_enabled` | runtime logging path for both directions | logging flags enabled, inbound domain message that triggers output | stdout contains one inbound and one outbound automation log line |
