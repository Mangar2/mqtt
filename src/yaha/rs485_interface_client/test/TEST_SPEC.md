# rs485_interface_client test specification

## Scope

Unit tests for phase-1 runtime config contract and INI mapping in rs485_interface_client.

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
