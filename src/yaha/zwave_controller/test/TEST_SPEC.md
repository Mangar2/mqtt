# TEST_SPEC.md — yaha/zwave_controller

All tests are tagged `[zwave_controller]`.

## zwave_controller_test.cpp — ZwaveController routing and event contract

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `set_value_routes_switch_payload_to_driver_set_value` | Regular set topic routes to `setValue` with converted switch bool | device mapping with switch type, topic `.../power/set`, payload `on` | fake driver receives one `setValue` call with bool `true` |
| `set_value_routes_configuration_class_to_set_config_param` | Config class (`0x70`) routes to `setConfigParam` | mapping row with class `0x70`, payload numeric | fake driver receives one config write with expected node/param/value |
| `controller_operations_forward_to_driver_port` | add/remove/scan/request/close are forwarded | call controller operations in sequence | fake driver call counters and arguments match expected values |
| `on_value_changed_publishes_mapped_switch_as_on_off` | Callback event mapped publish conversion for switch type | node/value event with mapped switch and numeric on payload | publishes mapped topic with value `on` |
| `on_controller_command_publishes_monitoring_notification` | Controller feedback callback publish contract | result code and status text | publishes `$MONITORING/zwave/notification` with status text |
