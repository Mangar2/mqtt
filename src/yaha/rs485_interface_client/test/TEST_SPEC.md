# rs485_interface_client test specification

## Scope

Unit tests for phase-1 runtime config contract and INI mapping in rs485_interface_client.

Phase-6 extension:
- runtime composition and lifecycle integration checks for component + adapter + MQTT runtime wiring
- serial open/send failure-path verification for adapter behavior used by standalone runtime

## Planned and implemented test cases

1. rs485_runtime_config_parses_all_required_sections
- Scenario: full valid INI with mqtt, rs485interface core fields, interfaces, settings, status, addresses, and topics.
- Expected: parsing succeeds and all typed fields contain expected values.

2. rs485_runtime_config_rejects_missing_serial_port
- Scenario: rs485interface.serialPortName missing.
- Expected: parsing fails with error containing rs485interface.serialPortName.

3. rs485_runtime_config_rejects_invalid_trace_value
- Scenario: rs485interface.trace contains unsupported value.
- Expected: parsing fails with error containing rs485interface.trace.

4. rs485_runtime_config_rejects_missing_required_interfaces_section
- Scenario: section rs485interface.interfaces missing.
- Expected: parsing fails with deterministic missing-section error.

5. rs485_runtime_config_rejects_invalid_topic_mapping_format
- Scenario: topics entry does not follow COMMAND,VALUE,ADDRESS format.
- Expected: parsing fails with error pointing to rs485interface.topics format violation.

6. rs485_runtime_config_rejects_invalid_interface_map_value
- Scenario: interface map contains non-numeric value.
- Expected: parsing fails with error pointing to rs485interface.interfaces value parsing.

7. rs485_runtime_build_creates_component_adapter_mqtt_runtime_objects
- Scenario: valid parsed runtime config is passed to runtime builder.
- Expected: build succeeds and all runtime object pointers are initialized.

8. rs485_runtime_component_startup_and_shutdown_is_clean
- Scenario: build runtime, start component threads, then close.
- Expected: no exception and clean shutdown.

9. rs485_serial_adapter_open_fails_for_invalid_path
- Scenario: open adapter with invalid serial device path.
- Expected: open returns false and error message is non-empty.

10. rs485_serial_adapter_send_fails_when_not_open
- Scenario: call send before opening serial adapter.
- Expected: send returns false with deterministic error text.
