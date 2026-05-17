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
| `rs485_topic_mapper_to_mqtt_uses_explicit_topics_and_switch_bits` | explicit inbound mapping with switch bits | serial frame matching explicit rule and bit masks | publishes explicit topic with `on`/`off` according to bit semantics |
| `rs485_topic_mapper_to_mqtt_falls_back_to_address_and_status_mapping` | generic inbound fallback path | serial frame not mapped by `topics` but known address+status+interface | publishes one topic with mapped string value |
| `rs485_topic_mapper_to_mqtt_rejects_unknown_command` | unknown command mapping error path | serial frame with unknown command | throws `Unknown serial command ...` |
| `rs485_interface_component_derives_expected_subscriptions` | component subscription contract | config with addresses, settings, and explicit topics | wildcard `/set`, explicit `topic/+`, and monitor/system wildcard topics are present with configured QoS |
| `rs485_interface_component_set_action_emits_serial_message_after_enable_send` | MQTT `/set` action runtime behavior | run component, enqueue `/set`, then feed enable-send token | serial callback receives encoded command frame for mapped address/command/value |
| `rs485_interface_component_serial_input_publishes_mapped_mqtt_message` | serial->MQTT runtime behavior | feed one non-token serial frame matching mapping config | publish callback receives mapped topic/value with configured QoS |
| `rs485_interface_component_accepts_trace_topics_in_sys_and_monitor_namespace` | trace control-topic handling | send trace-set on `$SYS` and `$MONITOR` namespace | no exception and action pipeline remains functional |
