# automation_client_rule_runtime

## Purpose

Encapsulates runtime rule gating and delivery behavior for automation rules.
The module owns event-history ingestion, gate evaluation, trace explanation,
and delivery suppression (dedup, delay, cooldown).

## Public API

- `rule_runtime_engine.h` exposes runtime state structs and `RuleRuntimeEngine`.

## Internal module split

- `rule_topic_filter.*`
  - Topic matching (`+`, `#`), rule-node topic-shape validation, path joining.
- `rule_field_access.*`
  - Typed reads for optional numeric and topic-filter rule fields.
- `rule_time_gate.*`
  - `active`, `weekdays`, `time` and `duration` gate parsing/evaluation.
- `rule_event_gate.*`
  - Event/inactivity gate evaluation and trigger-reason formatting.
- `rule_event_gate_trace.*`
  - Human-readable gate-trace lines used by debug preview.
- `rule_delivery_control.*`
  - Delivery suppression state machine and helper utilities.
- `rule_runtime_engine.*`
  - Orchestrator wiring all gates with `SingleRuleProcessor`.

## Behavior contract

- No behavior change compared to previous monolithic runtime-engine implementation.
- Trace semantics and reason text remain parity-compatible.
- Event-state and delivery-state mutation semantics remain unchanged.
