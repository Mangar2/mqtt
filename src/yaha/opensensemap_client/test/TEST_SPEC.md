# OpenSenseMap Client Config Test Specification

## Test cases

1. `load_runtime_config_parses_opensensemap_and_sensor_sections`
- Scenario: INI contains valid `[opensensemap]` and repeated `[sensor]` sections.
- Input: in-memory INI file with two sensors.
- Expected: parser returns success with two sensor mappings and configured fields.

2. `load_config_rejects_legacy_uint_sensor_key`
- Scenario: sensor section uses key `uint`.
- Input: INI with `[sensor]` using `uint` instead of `unit`.
- Expected: parser fails with explicit error message to use `unit`.

3. `load_config_requires_sensor_entries`
- Scenario: no `[sensor]` section exists.
- Input: INI with only `[opensensemap]`.
- Expected: parser fails with missing sensor message.

4. `load_config_requires_opensensemap_id`
- Scenario: missing required `opensensemap.id`.
- Input: INI missing id key.
- Expected: parser fails with required id message.
