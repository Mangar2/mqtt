# Pushover Client Config Test Specification

## Test cases

1. `load_runtime_config_parses_pushover_devices_and_subscriptions`
- Scenario: INI contains valid `[pushover]`, repeated `[device]`, and repeated `[subscription]` sections.
- Input: in-memory INI file.
- Expected: parser returns success with parsed host, credentials, devices, and subscriptions.

2. `load_config_requires_device_entries`
- Scenario: no `[device]` section exists.
- Input: INI with valid `[pushover]` and `[subscription]`.
- Expected: parser fails with missing device message.

3. `load_config_requires_subscription_entries`
- Scenario: no `[subscription]` section exists.
- Input: INI with valid `[pushover]` and `[device]`.
- Expected: parser fails with missing subscription message.

4. `load_config_requires_token_and_user`
- Scenario: required authentication keys are missing.
- Input: INI without `token` and `user`.
- Expected: parser fails with required key message.

5. `load_config_rejects_subscription_qos_without_topic`
- Scenario: structured subscription ordering validation.
- Input: `[subscription]` with `qos` before `topic`.
- Expected: parser fails with qos-order error.

6. `load_config_rejects_duplicate_subscription_qos`
- Scenario: one subscription entry defines `qos` twice.
- Input: `[subscription]` with duplicate qos keys.
- Expected: parser fails with duplicate-qos error.

7. `load_runtime_config_falls_back_on_invalid_mqtt_values`
- Scenario: runtime loader keeps defaults when MQTT parser rejects one value.
- Input: invalid `mqtt.loopSleepMs=0` with valid pushover config.
- Expected: runtime load succeeds and mqtt defaults are kept.

8. `pushover_request_sender_parses_successful_curl_output`
- Scenario: sender factory parses curl output metadata.
- Input: stub curl output with payload + status/content-type markers.
- Expected: sender returns parsed response.

9. `pushover_request_sender_throws_on_non_zero_curl_exit`
- Scenario: curl exits with non-zero code.
- Input: stub curl exits with error.
- Expected: sender throws runtime error.

10. `pushover_request_sender_throws_on_missing_metadata`
- Scenario: curl output misses metadata markers.
- Input: stub curl returns body only.
- Expected: sender throws parse error.

11. `load_config_rejects_invalid_device_shape`
- Scenario: strict parser rejects unsupported device key.
- Input: `[device]` with key `id` instead of `name`.
- Expected: parser fails with invalid-key message.

12. `load_runtime_config_falls_back_on_invalid_pushover_port_and_mqtt`
- Scenario: optional pushover/mqtt numeric values are invalid.
- Input: invalid `pushover.port` and invalid `mqtt.port`.
- Expected: runtime load succeeds and defaults are retained.

13. `pushover_request_sender_throws_on_invalid_status_metadata`
- Scenario: curl metadata has non-numeric status code.
- Input: stub curl returns `__YAHA_STATUS__:abc`.
- Expected: sender throws status-parse error.
