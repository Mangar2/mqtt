# TEST_SPEC.md — yaha/zwave_devices

All tests are tagged `[zwave_devices]`.

## zwave_devices_mapper_test.cpp — ZwaveDevicesMapper parity core

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `value_to_topic_prefers_more_specific_mapping` | Match scoring prefers exact class/index/instance over broad row | descriptor with matching broad+specific config rows | returns specific topic and type |
| `value_to_topic_appends_label_when_class_is_unspecified` | Legacy label append rule for class-unspecified mapping | descriptor with label and matched row without class id | topic is `config.topic/label` |
| `topic_to_id_uses_label_lookup_when_class_id_missing` | Topic-to-id fallback class resolution by label in node map | mapping row without class id and node object list with label+instance | resolved id copies class/index/type from node object |
| `build_write_request_routes_config_class_to_set_config_param` | Configuration command class routes to config write path | resolved id with class `0x70` and numeric value | request kind `SetConfigParam` with numeric payload |
| `build_write_request_converts_switch_on_to_boolean_true` | Switch type conversion parity | resolved id type `switch`, input `on` | request kind `SetValue` and bool payload `true` |
