# automation_client_rule_runtime test specification

## Scope

Unit tests for runtime gate and delivery-control behavior exposed through
`RuleRuntimeEngine`.

## Existing migrated cases

- `rule_runtime_engine_validates_rule_topic_shapes`
- `rule_runtime_engine_ingests_motion_events_and_trims_history`
- `rule_runtime_engine_reports_time_gate_errors_and_previews_delivery`
- `rule_runtime_engine_covers_time_duration_and_weekday_gates`
- `rule_runtime_engine_covers_event_gates_allow_noneof_and_inactivity`
