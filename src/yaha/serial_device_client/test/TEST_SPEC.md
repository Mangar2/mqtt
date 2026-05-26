# serial_device_client test specification

## Scope

Unit tests for phase-1 INI mapping and runtime config loading in serial_device_client.

## Planned and implemented test cases

1. serial_device_config_rejects_missing_serial_port_name
- Scenario: [serialdevice] section exists without serialPortName.
- Expected: loader returns false and error mentions serialdevice.serialPortName.

2. serial_device_config_applies_defaults_for_invalid_numeric_values
- Scenario: invalid baudrate, qos, and keepAliveDelayInSeconds values are provided.
- Expected: loader succeeds and default values are kept.

3. serial_device_config_parses_interface_mappings_and_value_map
- Scenario: all relevant serialdevice interface sections are present and valid.
- Expected: command/receiver/send/topic/value maps are parsed into typed fields.

4. serial_device_config_marks_receiver_map_as_provided_for_empty_section
- Scenario: [serialdevice.serial.receiverMap] section exists but has no entries.
- Expected: receiverMapProvided is true and receiverMap is empty.

5. serial_device_runtime_config_loads_domain_and_mqtt_values
- Scenario: valid [serialdevice] and [mqtt] sections.
- Expected: runtime config loader succeeds and maps both domain and mqtt values.

6. serial_device_runtime_config_keeps_mqtt_defaults_on_invalid_mqtt_values
- Scenario: [mqtt] section has invalid optional value.
- Expected: runtime config loader succeeds and mqtt defaults remain for invalid field.
