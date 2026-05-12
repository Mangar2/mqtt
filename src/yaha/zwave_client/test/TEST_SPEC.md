# TEST_SPEC.md — yaha/zwave_client

All tests are tagged `[zwave_client]`.

## zwave_client_app_test.cpp — runtime config schema and integration mapping

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `load_zwave_config_applies_defaults_and_parses_required_device` | Unit validation for defaults and required fields | minimal valid `[zwave]` section with one `device` row | parse success; default qos/retain values preserved; required usb/topic/device loaded |
| `load_zwave_config_rejects_invalid_device_row` | Unit validation for schema bounds | `zwave.device` row with invalid node id | parse fails with deterministic error containing `nodeId` range text |
| `load_zwave_config_rejects_invalid_device_field_count` | Device entry must include topic and node id | `zwave.device` row with only topic field | parse fails with deterministic field-count error |
| `load_zwave_config_rejects_empty_device_topic` | Device topic must be non-empty | `zwave.device` row with empty topic token | parse fails with deterministic topic-empty error |
| `load_zwave_config_rejects_invalid_optional_numeric_fields` | Optional class/instance/index bounds and parsing | three configs with invalid classId/instance/index values | parse fails with deterministic field-specific error text |
| `load_zwave_config_rejects_invalid_qos_and_retain_values` | QoS/retain parser validation | invalid `subscribeQoS`, invalid `qos`, invalid `retain` | parse fails and error references corresponding key |
| `load_zwave_config_requires_usb_settings` | Mandatory USB settings | configs missing `usbDevice` or missing `usbTopic` | parse fails with missing-setting error |
| `load_zwave_runtime_config_combines_zwave_and_mqtt_sections` | Runtime integration mapping of combined config | valid `[zwave]` and `[mqtt]` fields | parse success; zwave and mqtt outputs reflect configured values |
| `load_zwave_runtime_config_reports_mqtt_validation_error` | Runtime integration error propagation | invalid mqtt port value | parse fails and reports mqtt range validation error |
| `load_zwave_config_requires_device_setting` | Unit validation for mandatory device list | `[zwave]` without `device` key | parse fails with `missing required setting 'zwave.device'` |
