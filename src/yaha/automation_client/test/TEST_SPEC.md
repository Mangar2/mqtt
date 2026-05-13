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
| `load_automation_client_runtime_config_from_ini` | config mapping for filestore and automation fields | ini with mqtt, filestore and automation entries | parsed config values match inputs |
| `load_automation_client_runtime_config_defaults_logging_flags_to_false` | defaults for optional logging switches | ini without automation logging flags | both logging flags in runtime config remain false |
| `load_automation_client_runtime_config_reports_invalid_log_incoming_messages` | invalid bool validation for incoming logging flag | ini with `automation.logIncomingMessages=maybe` | loader fails and reports field-specific error |
| `load_automation_client_runtime_config_reports_invalid_log_outgoing_messages` | invalid bool validation for outgoing logging flag | ini with `automation.logOutgoingMessages=maybe` | loader fails and reports field-specific error |
| `automation_component_logs_incoming_and_outgoing_messages_when_enabled` | runtime logging path for both directions | logging flags enabled, inbound domain message that triggers output | stdout contains one inbound and one outbound automation log line |
| `automation_component_management_update_persist_failure_rolls_back_and_acks` | update command with file-store persistence failure | existing stable rule, `POST /automation/rules` returns non-200 | in-memory rules remain unchanged and ack payload is `persist_failed` |
| `automation_component_management_delete_persist_failure_rolls_back_and_acks` | delete command with file-store persistence failure | existing rule, delete payload, `POST /automation/rules` returns non-200 | deleted rule is restored and ack payload is `persist_failed` |
| `automation_component_publish_failure_logs_out_fail_without_false_out_success` | outbound callback throws while outgoing logging is enabled | callback throws on publish and rule evaluation emits message | stderr contains `automation_client[out-fail]` and no `automation_client[out]` success line for that message |
| `automation_component_retries_management_ack_after_transient_publish_failure` | first management ack send fails then retry succeeds | callback returns `PublishResult::fail(AckTimeout)` once then success, follow-up inbound message triggers retry loop | ack message is eventually published successfully |
| `automation_component_retry_budget_exhaustion_logs_failure` | retry queue stops after configured max attempts | callback always throws and repeated inbound messages drive retry loop | log contains `category=retry_exhausted` |
| `automation_component_run_with_filestore_get_failure_keeps_empty_rules` | startup load fails with file store GET error | file store returns non-200 for `GET /automation/rules` | component starts, keeps empty rules, and does not crash |
| `automation_component_monitor_reload_get_failure_keeps_existing_rules` | monitor-triggered reload fails on file store GET error | existing loaded rule, monitor changed event, then file store returns non-200 | existing rules remain unchanged after failed reload |
| `automation_component_flushes_retry_queue_after_callback_restore` | queued publish retries are flushed after callback becomes available again | first management ack is queued due to missing callback, callback set later, next inbound message triggers retry processing | queued management ack is published successfully |
