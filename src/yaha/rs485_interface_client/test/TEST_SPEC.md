# rs485_interface_client test specification

## Scope

Unit tests for phase-1 runtime config contract and INI mapping in rs485_interface_client.

Phase-6 extension:
- runtime composition and lifecycle integration checks for component + adapter + MQTT runtime wiring
- serial open/send failure-path verification for adapter behavior used by standalone runtime

## Planned and implemented test cases

1. rs485_runtime_config_parses_rs485_core_transport_fields
- Scenario: full valid INI with mqtt and rs485interface core transport values.
- Expected: parsing succeeds and core RS485 transport fields map correctly.

2. rs485_runtime_config_parses_rs485_behavior_fields
- Scenario: full valid INI with rs485 runtime behavior values.
- Expected: parsing succeeds and qos/trace/logIncomingMessages/logOutgoingMessages/blink/temporary fields map correctly.

3. rs485_runtime_config_parses_mqtt_connection_fields
- Scenario: full valid INI with mqtt section values.
- Expected: parsing succeeds and mqtt host/port/clientId map correctly, and MQTT message trace is enabled when rs485 logIncomingMessages or logOutgoingMessages is true.

4. rs485_runtime_config_parses_interfaces_and_value_map
- Scenario: full valid INI with rs485interface.interfaces section.
- Expected: parsing succeeds and usedBy/map entries are loaded correctly.

5. rs485_runtime_config_parses_command_address_and_topic_sections
- Scenario: full valid INI with settings/status/addresses/topics sections.
- Expected: parsing succeeds and command/address/topic mappings are loaded correctly.

6. rs485_runtime_config_rejects_missing_serial_port
- Scenario: rs485interface.serialPortName missing.
- Expected: parsing fails with error containing rs485interface.serialPortName.

7. rs485_runtime_config_rejects_invalid_trace_value
- Scenario: rs485interface.trace contains unsupported value.
- Expected: parsing fails with error containing rs485interface.trace.

8. rs485_runtime_config_rejects_missing_required_interfaces_section
- Scenario: section rs485interface.interfaces missing.
- Expected: parsing fails with deterministic missing-section error.

9. rs485_runtime_config_rejects_invalid_topic_mapping_format
- Scenario: topics entry does not follow COMMAND,VALUE,ADDRESS format.
- Expected: parsing fails with error pointing to rs485interface.topics format violation.

10. rs485_runtime_config_rejects_invalid_interface_map_value
- Scenario: interface map contains non-numeric value.
- Expected: parsing fails with error pointing to rs485interface.interfaces value parsing.

11. rs485_runtime_build_creates_all_runtime_object_pointers
- Scenario: valid parsed runtime config is passed to runtime builder.
- Expected: build succeeds and all runtime object pointers are initialized.

12. rs485_runtime_build_opens_serial_adapter
- Scenario: valid parsed runtime config is passed to runtime builder.
- Expected: build succeeds and serial adapter is open.

13. rs485_runtime_build_fails_when_serial_open_fails
- Scenario: runtime build is requested with invalid serial path.
- Expected: build fails and error contains code RS485_RUNTIME_SERIAL_OPEN_FAILED.

14. rs485_runtime_component_startup_and_shutdown_is_clean
- Scenario: build runtime, start component threads, then close.
- Expected: no exception and clean shutdown.

15. rs485_serial_adapter_open_fails_for_invalid_path
- Scenario: open adapter with invalid serial device path.
- Expected: open returns false and error message is non-empty.

16. rs485_serial_adapter_send_fails_when_not_open
- Scenario: call send before opening serial adapter.
- Expected: send returns false with deterministic error text.

17. rs485_runtime_config_rejects_invalid_command_keys_and_topics_values
- Scenario: invalid single-character command keys and invalid topic mapping value/address tokens.
- Expected: parsing fails with field-specific validation messages.

18. rs485_runtime_config_rejects_invalid_interface_segment_shapes
- Scenario: malformed interface definitions (missing usedby, invalid usedby token, invalid map token).
- Expected: parsing fails with deterministic interface validation error.

19. rs485_serial_adapter_send_writes_payload_to_serial_master
- Scenario: adapter opened on pseudo-terminal slave sends payload.
- Expected: bytes are readable unchanged from pseudo-terminal master fd.

20. rs485_serial_adapter_receive_callback_gets_serial_bytes
- Scenario: bytes written to pseudo-terminal master while callback is installed.
- Expected: receive callback is invoked with the transmitted payload bytes.

21. rs485_serial_adapter_open_supports_all_configured_baudrates
- Scenario: open adapter on pseudo-terminal slave with each supported configured baudrate.
- Expected: open succeeds for every configured baudrate path and adapter closes cleanly afterwards.

22. rs485_serial_adapter_receive_without_callback_is_ignored
- Scenario: open adapter, write bytes to pseudo-terminal master, and keep receive callback unset.
- Expected: read loop consumes bytes without exception and close remains clean.

23. rs485_serial_adapter_open_with_unknown_baudrate_uses_default_mapping
- Scenario: open adapter on pseudo-terminal with unsupported baudrate value.
- Expected: adapter opens successfully using default baudrate mapping path and closes cleanly.

24. rs485_serial_adapter_close_is_idempotent
- Scenario: call close repeatedly without opening adapter first.
- Expected: close is a no-op and never throws.

25. rs485_serial_adapter_open_fails_for_non_tty_device
- Scenario: open adapter against a non-TTY path (`/dev/null`) where `open()` succeeds but termios attribute read fails.
- Expected: open throws with deterministic `failed to read serial attributes` details and adapter remains closed.

26. rs485_serial_adapter_send_reports_write_failure
- Scenario: open adapter on pseudo-terminal slave, close pseudo-terminal master, then send payload.
- Expected: send throws with deterministic `failed to write serial data` details and adapter closes cleanly.
