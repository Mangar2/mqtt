# rs485_interface test specification

## Scope

Unit tests for RS485 phase-2 mapping and phase-6 component behavior verification.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `rs485_topic_mapper_to_serial_uses_explicit_topic_bit_mapping` | explicit `topics` mapping path for outbound command | MQTT topic listed in `topics` with payload `on` and `off` | serial command/address from rule and value with switch bit prefix |
| `rs485_topic_mapper_to_serial_uses_address_command_and_interface_mapping` | generic topic path with `addresses`, `settings`, and `interfaces` | MQTT topic matching configured prefix/suffix and value `on` | serial address and command resolved and mapped value returned |
| `rs485_topic_mapper_to_serial_rejects_unknown_topic_prefix` | missing address mapping error path | MQTT topic without matching address prefix | throws `undefined device address ...` |
| `rs485_topic_mapper_to_serial_rejects_unknown_value_mapping` | missing integer/interface mapping error path | topic resolves command but value text has no mapping | throws integer-mapping error |
| `rs485_topic_mapper_to_serial_accepts_numeric_payload_and_rejects_fractional` | numeric payload conversion path and non-integer rejection | generic mapped topic with values `1.0` and `1.5` | integer value converts to serial `1`, fractional value throws integer-mapping error |
| `rs485_topic_mapper_to_serial_rejects_out_of_range_numeric_payload` | uint16 range enforcement for numeric payload | generic mapped topic with value greater than `65535` | throws positive-two-byte range error |
| `rs485_topic_mapper_to_serial_rejects_unknown_setting_suffix` | unknown setting suffix error path | known address prefix but unknown setting suffix | throws `undefined device setting ...` |
| `rs485_topic_mapper_to_mqtt_uses_explicit_topics_and_switch_bits` | explicit inbound mapping with switch bits | serial frame matching explicit rule and bit masks | publishes explicit topic with `on`/`off` according to bit semantics |
| `rs485_topic_mapper_to_mqtt_falls_back_to_address_and_status_mapping` | generic inbound fallback path | serial frame not mapped by `topics` but known address+status+interface | publishes one topic with mapped string value |
| `rs485_topic_mapper_to_mqtt_rejects_unknown_command` | unknown command mapping error path | serial frame with unknown command | throws `Unknown serial command ...` |
| `rs485_topic_mapper_to_mqtt_covers_non_switch_and_numeric_fallback` | explicit non-switch bit mapping and numeric fallback value path | explicit topic rule with non-switch value and fallback serial value not present in interface map | explicit message maps to `on/off` by bit and fallback message keeps numeric payload |
| `rs485_topic_mapper_to_mqtt_rejects_unknown_sender_address` | unknown serial sender mapping error path | fallback decode with sender address not in config | throws `Unknown serial address ...` |
| `rs485_interface_component_derives_expected_subscriptions` | component subscription contract | config with addresses, settings, and explicit topics | wildcard `/set`, explicit `topic/+`, and monitor/system wildcard topics are present with configured QoS |
| `rs485_interface_component_set_action_emits_serial_message_after_enable_send` | MQTT `/set` action runtime behavior | run component, enqueue `/set`, then feed enable-send token | serial callback receives encoded command frame for mapped address/command/value |
| `rs485_interface_component_serial_input_publishes_mapped_mqtt_message` | serial->MQTT runtime behavior | feed one non-token serial frame matching mapping config | publish callback receives mapped topic/value with configured QoS |
| `rs485_interface_component_accepts_trace_topics_in_sys_and_monitor_namespace` | trace control-topic handling | send trace-set on `$SYS` and `$MONITOR` namespace | no exception and action pipeline remains functional |
| `rs485_interface_component_ignores_unknown_action_suffix` | unknown action suffix error branch | run component and send topic with unsupported action suffix | no serial command is emitted and component remains stable |
| `rs485_interface_component_trace_topics_accept_non_string_payload_without_change` | trace topic branch with non-string payload | `$SYS` and `$MONITOR` trace set topics with numeric payload | no exception and message is ignored safely |
| `rs485_interface_component_run_and_close_are_idempotent` | lifecycle idempotence branch coverage | call `close` before run, call `run` twice, call `close` twice | no exception and lifecycle transitions remain stable |
| `rs485_interface_component_decode_and_map_errors_are_handled` | decode-error and mapping-error logging paths | invalid raw serial bytes and decodable serial message with unknown sender mapping | `feedSerialBytes` handles both error paths without throwing |
| `rs485_interface_component_log_flags_cover_incoming_and_outgoing_paths` | incoming/outgoing log branches for MQTT+serial paths | component with both log flags enabled, one `/set` message, one enable-send token, and one inbound serial message | incoming/outgoing MQTT+serial log paths execute without exceptions |
| `rs485_interface_component_get_subscriptions_covers_join_topic_variants` | subscription path generation branches for topic join rules | settings suffix with and without leading slash and empty suffix | generated subscriptions include expected wildcard paths for each join variant |
| `rs485_interface_component_inbound_publish_without_callback_is_ignored` | publish path branch when no callback is installed | inbound decodable serial message that maps to MQTT message without publish callback setup | mapped publish path is reached and safely ignored without exception |
| `rs485_interface_component_blink_actions_when_stopped_do_not_throw` | blink parsing and worker-launch path while component is stopped | `/blink` messages with valid integer text, non-integer numeric value, and zero text | blink action parsing and worker-thread lifecycle complete without exceptions |
| `rs485_interface_component_numeric_state_cache_and_cached_blink_path` | numeric cache-update branch and cached-state read branch | interfaces map cleared, inbound numeric serial state update, then stopped `/blink` message | numeric state cache branch is used and cached blink path executes safely |
