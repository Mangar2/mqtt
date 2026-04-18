# TODO: structured_tracing_json_lines

## Problem
The broker has no structured tracing. Failures and MQTT lifecycle events cannot be filtered consistently by severity and module, and trace configuration cannot be controlled uniformly from config, CLI, and runtime system messages.

## Action
Introduce a structured tracing system close to `std::io` / stream-style output with JSON Lines records and hierarchical levels `none`, `error`, `warning`, `info`, `trace`.

The system must support:
- one global threshold for `error`, `warning`, and `info`
- per-module `trace` enablement in addition to the global threshold
- configuration through config file, CLI parameters, and runtime system messages
- JSON-line fields `timestamp`, `level`, `module`, `info`, with optional `detail` and `data`

Initial implementation scope:
- build only the tracing infrastructure
- emit `info` traces for CONNECT handling as the first verification step
- defer broader MQTT event coverage to follow-up requirements