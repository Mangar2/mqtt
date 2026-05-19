# TEST_SPEC.md — yaha/zwave_controller

All tests are tagged `[zwave_controller]`.

## zwave_controller_test.cpp — ZwaveController routing and event contract

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `set_value_routes_switch_payload_to_driver_set_value` | Regular set topic routes to `setValue` with converted switch bool | device mapping with switch type, topic `.../power/set`, payload `on` | fake driver receives one `setValue` call with bool `true` |
| `set_value_routes_configuration_class_to_set_config_param` | Config class (`0x70`) routes to `setConfigParam` | mapping row with class `0x70`, payload numeric | fake driver receives one config write with expected node/param/value |
| `set_value_routes_multilevel_without_explicit_type_as_bool_for_legacy_parity` | Multilevel shutter rows without explicit type must keep legacy bool conversion | mapping row with class `0x26`, topic `.../shutter/.../set`, payload `on` | fake driver receives one `setValue` call with target type `bool` and bool payload `true` |
| `controller_operations_forward_to_driver_port` | add/remove/scan/request/close are forwarded | call controller operations in sequence | fake driver call counters and arguments match expected values |
| `on_value_changed_publishes_mapped_switch_as_on_off` | Callback event mapped publish conversion for switch type | node/value event with mapped switch and numeric on payload | publishes mapped topic with value `on` |
| `on_value_refreshed_updates_cache_without_publishing` | Refreshed callback event must update cache only and must not emit outbound publish | node/value refreshed event with mapped switch and numeric on payload, then changed event | no publish on refreshed event; publish occurs on changed event |
| `on_controller_command_publishes_monitoring_notification` | Controller feedback callback publish contract | result code and status text | publishes `$MONITOR/zwave/notification` with status text |
| `set_value_rejects_topic_without_trailing_set` | Invalid set topic shape must be rejected | topic without trailing `/set` | throws runtime error |
| `remove_failed_node_rejects_non_numeric_or_fractional_values` | removeFailedNode must accept only integer node ids | string `abc` and fractional numeric value | throws runtime error |
| `driver_lifecycle_callbacks_publish_expected_monitoring_messages` | Driver lifecycle callbacks publish deterministic monitoring events | invoke `onDriverReady`, `onDriverFailed`, `onScanComplete` | publishes expected topics, values, and reason details |
| `notification_callback_maps_all_codes_and_uses_unknown_topic_fallback` | Notification callback should map each notification code and fallback topic for unknown nodes | invoke `onNotification` with all enum values on unmapped node id | publishes to `/$MONITOR/zwave/unknown node <id>` with expected text values |
| `node_ready_does_not_enable_global_polling` | Node ready should not enable legacy global polling anymore | add switch and non-switch values, then mark node ready | `enablePoll` is not called |
| `on_value_changed_for_usb_controller_publishes_to_usb_topic` | USB controller node id must publish to configured USB topic | value change event with node id 1 | publish topic equals configured usb topic |
| `on_value_changed_without_mapping_falls_back_to_monitoring_topic` | Missing mapping should fallback to monitoring publish path | value change event on unmapped node | publishes to `$MONITOR/zwave/<nodeId>` |
| `matching_feedback_prepends_tracked_reasons` | Matching feedback event prepends remembered command reasons to outbound message | tracked `on` command with two reasons, then matching value change | outbound reason list begins with remembered reasons in original order |
| `same_action_replaces_pending_entry` | Repeated identical action replaces older pending command entry | track same reply-topic+target+value twice with different reasons, then matching value change | outbound message uses only latest reasons |
| `different_feedback_keeps_pending_command` | Non-matching feedback must not remove pending command | track `on`, receive `off`, then receive `on` | first publish has no tracked reason; second publish includes tracked reason |
| `pending_command_times_out_and_is_removed` | Pending command is dropped after timeout even if match arrives later | short timeout, track command, wait beyond timeout, then matching value change | outbound message no longer includes tracked reason |
| `pending_command_polling_targets_only_affected_node` | Polling loop queries only nodes with tracked pending commands | track command for one node and wait for poll cycle | driver receives `requestNodeState` for the targeted node |
| `matching_value_refreshed_publishes_pending_feedback` | Refreshed value callback resolves pending command and publishes immediate feedback | track command, then receive matching `onValueRefreshed` event | outbound message is published on mapped topic and prepends tracked reason |
