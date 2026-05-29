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

5. `load_config_rejects_sensor_fields_without_name`
- Scenario: `[sensor]` value keys appear before `name`.
- Input: sensor block starting with `unit/topic/id`.
- Expected: parser fails with deterministic ordering error.

6. `load_config_rejects_duplicate_sensor_topic`
- Scenario: one sensor entry defines `topic` more than once.
- Input: `[sensor]` with duplicate topic keys.
- Expected: parser fails with duplicate topic error.

7. `load_config_rejects_incomplete_sensor_entry`
- Scenario: sensor entry misses required key.
- Input: `[sensor]` without `id`.
- Expected: parser fails with incomplete sensor error.

8. `load_runtime_config_falls_back_on_invalid_mqtt_values`
- Scenario: runtime loader keeps defaults when MQTT parser rejects one value.
- Input: invalid `mqtt.loopSleepMs=0` with otherwise valid opensensemap config.
- Expected: runtime load succeeds and mqtt defaults are kept.

9. `opensensemap_request_sender_parses_successful_curl_output`
- Scenario: sender factory parses curl output metadata.
- Input: stub curl command output containing payload, status marker, content-type marker.
- Expected: sender returns parsed status/payload/content type.

10. `opensensemap_request_sender_throws_on_non_zero_curl_exit`
- Scenario: curl command exits with non-zero code.
- Input: stub curl exits with error.
- Expected: sender throws runtime error.

11. `opensensemap_request_sender_throws_on_missing_metadata`
- Scenario: curl output misses metadata markers.
- Input: stub curl returns body without status/content-type markers.
- Expected: sender throws parse error.

12. `load_config_rejects_unknown_sensor_key`
- Scenario: strict parser rejects unsupported sensor key.
- Input: `[sensor]` with unknown key `foo`.
- Expected: parser fails with invalid-key message.

13. `load_runtime_config_falls_back_on_invalid_opensensemap_fields`
- Scenario: optional opensensemap and mqtt values are invalid.
- Input: invalid opensensemap `port/qos/useTls` and invalid mqtt `port`.
- Expected: runtime load succeeds and defaults are retained.

14. `opensensemap_request_sender_throws_on_invalid_status_metadata`
- Scenario: curl metadata has non-numeric status code.
- Input: stub curl returns `__YAHA_STATUS__:abc`.
- Expected: sender throws status-parse error.
