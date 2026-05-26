# serial_device test specification

## Scope

Unit tests for phase-1 and phase-2 serial_device parity contracts:
- subscription derivation
- serial stream parser
- wire serialization

## Planned and implemented test cases

1. derive_subscriptions_always_includes_system_topic
- Scenario: empty interface map.
- Expected: output includes $SYS/serialdevice/# with configured QoS.

2. derive_subscriptions_adds_topic_map_topics_with_plus_suffix
- Scenario: switch topic map contains one topic entry.
- Expected: output contains <topic>/+ with configured QoS.

3. derive_subscriptions_without_receiver_map_uses_command_suffix_only
- Scenario: receiverMapProvided is false with commandMap entries.
- Expected: output contains <commandSuffix>/+ entries without receiver prefix.

4. derive_subscriptions_with_receiver_map_uses_prefix_and_suffix
- Scenario: receiverMapProvided is true with receiverMap and commandMap entries.
- Expected: output contains Cartesian product <receiverPrefix><commandSuffix>/+.

5. derive_subscriptions_with_empty_receiver_map_and_provided_flag_adds_no_command_topics
- Scenario: receiverMapProvided is true and receiverMap is empty.
- Expected: output has no commandMap-derived topics and still includes system topic.

6. serial_device_subscriptions_match_oracle_e_fixture
- Scenario: load Oracle E subscription suite generated from legacy serialdevice and map each case input into C++ SerialDeviceConfig.
- Expected: derived subscription map matches oracle expected topics and QoS exactly for every case.

7. serial_device_parser_matches_oracle_a_fixtures
- Scenario: load Oracle A parser suites (`core.json`, `malformed.json`) and feed each case as chunked serial input stream.
- Expected: parsed message sequence matches oracle exactly (interface, sender, receiver, command, value, action), including malformed-frame recovery behavior.

8. serial_device_wire_serialization_matches_oracle_d_fixture
- Scenario: load Oracle D wire serialization suite and map each oracle input object to `SerialDeviceMessage`.
- Expected: serialized payload string matches oracle expected bytes/text exactly for all interface types in the fixture.

9. serial_device_message_to_string_formats_legacy_debug_layout
- Scenario: construct a serial message with mixed numeric/string endpoint fields.
- Expected: `toString()` output uses the exact legacy debug field order and text layout.

10. serial_device_parser_ignores_unknown_objects
- Scenario: feed noise plus unsupported object payload.
- Expected: payload is ignored and no parsed message is returned.

11. serial_device_parser_reassembles_partial_serial_object
- Scenario: feed one valid `{S,R,K,V}` object split across two chunks.
- Expected: first call returns no message, second call returns exactly one parsed serial message.

12. serial_device_parser_converts_switch_bit_string_value
- Scenario: parse array switch frame with bit-string value.
- Expected: switch value uses legacy right-to-left bit conversion semantics.

13. serial_device_wire_serializer_switch_routes_cover_supported_and_invalid_values
- Scenario: serialize switch messages for ON/OFF/no-flag/invalid-bit-index combinations.
- Expected: payloads follow `s<lsb><H|L>` behavior and return empty string for invalid target bit index.

14. serial_device_wire_serializer_serial_json_uses_legacy_field_order_and_escaping
- Scenario: serialize serial interface message with null sender and escapable receiver/command/value content.
- Expected: JSON output keeps legacy key order `S,R,C,V` and proper escaping.

15. serial_device_wire_serializer_handles_unknown_interface_and_non_numeric_switch_payload
- Scenario: serialize unsupported interface, non-numeric switch payload, and fs20 command without slash.
- Expected: unsupported/non-numeric paths return empty string and fs20 no-slash path follows current implementation output.

16. serial_device_mqtt_to_serial_matches_oracle_b_fixtures
- Scenario: run mapper against Oracle B core and errors suites.
- Expected: mapped serial messages match oracle field-by-field and error cases throw expected legacy error text exactly.

17. serial_device_serial_to_mqtt_matches_oracle_c_fixtures
- Scenario: run mapper against Oracle C core and switch suites.
- Expected: produced MQTT messages match oracle topic/value/reason/qos and message order exactly.

18. serial_to_mqtt_maps_numeric_value_via_reverse_value_map
- Scenario: serial interface message with numeric value matching configured reverse valueMap entry.
- Expected: mqtt value is converted to mapped interface text (for example `off`) on output.

19. serial_to_mqtt_throws_on_unknown_interface
- Scenario: serial message references an interface not configured in SerialDeviceConfig.
- Expected: mapper throws runtime error.

20. serial_to_mqtt_throws_on_unknown_sender_address
- Scenario: serial message sender does not match configured receiverMap entries.
- Expected: mapper throws runtime error for unknown serial address.

21. serial_to_mqtt_throws_on_unknown_command
- Scenario: serial interface command misses commandMap and sendMap entries.
- Expected: mapper throws runtime error for unknown serial command.

22. serial_to_mqtt_switch_branch_handles_non_matching_topic_entries
- Scenario: switch frame is processed while topicMap contains both matching and non-matching command entries.
- Expected: mapper skips non-matching entries and publishes only matching switch topics.

23. serial_to_mqtt_switch_with_monostate_sender_returns_no_matches
- Scenario: switch frame has monostate sender value.
- Expected: strict sender/address comparison causes no topic matches and returns an empty publish list.

24. serial_to_mqtt_sender_numeric_with_non_numeric_address_triggers_no_match
- Scenario: sender is numeric but receiverMap contains a non-numeric address token.
- Expected: numeric fallback comparison fails and mapper throws unknown-address runtime error.
