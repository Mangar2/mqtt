# TEST_SPEC.md — yaha/zwave_controller

All tests are tagged `[zwave_controller]`.

## zwave_controller_test.cpp — ZwaveController routing and event contract

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `set_value_routes_switch_payload_to_driver_set_value` | Regular set topic routes to `setValue` with converted switch bool | device mapping with switch type, topic `.../power/set`, payload `on` | fake driver receives one `setValue` call with bool `true` |
| `set_value_routes_configuration_class_to_set_config_param` | Config class (`0x70`) routes to `setConfigParam` | mapping row with class `0x70`, payload numeric | fake driver receives one config write with expected node/param/value |
| `controller_operations_forward_to_driver_port` | add/remove/scan/request/close are forwarded | call controller operations in sequence | fake driver call counters and arguments match expected values |
| `on_value_changed_publishes_mapped_switch_as_on_off` | Callback event mapped publish conversion for switch type | node/value event with mapped switch and numeric on payload | publishes mapped topic with value `on` |
| `on_controller_command_publishes_monitoring_notification` | Controller feedback callback publish contract | result code and status text | publishes `$MONITOR/zwave/notification` with status text |
| `set_value_rejects_topic_without_trailing_set` | Invalid set topic shape must be rejected | topic without trailing `/set` | throws runtime error |
| `remove_failed_node_rejects_non_numeric_or_fractional_values` | removeFailedNode must accept only integer node ids | string `abc` and fractional numeric value | throws runtime error |
| `driver_lifecycle_callbacks_publish_expected_monitoring_messages` | Driver lifecycle callbacks publish deterministic monitoring events | invoke `onDriverReady`, `onDriverFailed`, `onScanComplete` | publishes expected topics, values, and reason details |
| `notification_callback_maps_all_codes_and_uses_unknown_topic_fallback` | Notification callback should map each notification code and fallback topic for unknown nodes | invoke `onNotification` with all enum values on unmapped node id | publishes to `/$MONITOR/zwave/unknown node <id>` with expected text values |
| `node_ready_enables_poll_for_switch_classes` | Node ready should enable poll for switch class entries already cached in runtime state | add switch and non-switch values, then mark node ready | enablePoll called only for switch classes |
| `on_value_changed_for_usb_controller_publishes_to_usb_topic` | USB controller node id must publish to configured USB topic | value change event with node id 1 | publish topic equals configured usb topic |
| `on_value_changed_without_mapping_falls_back_to_monitoring_topic` | Missing mapping should fallback to monitoring publish path | value change event on unmapped node | publishes to `$MONITOR/zwave/<nodeId>` |
