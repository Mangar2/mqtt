# OpenSenseMap Component Test Specification

## Test cases

1. `subscriptions_include_all_configured_sensor_topics`
- Scenario: component initialized with two configured sensors.
- Input: `getSubscriptions()`.
- Expected: map contains both sensor topics with configured qos.

2. `handle_message_publishes_success_status_for_http_201`
- Scenario: sender returns HTTP 201 with JSON message payload.
- Input: numeric message for configured sensor.
- Expected: one status publish on `$SYS/opensensemap/success`, value `201`, reason contains extracted JSON message.

3. `handle_message_publishes_error_when_sensor_mapping_missing`
- Scenario: inbound topic has no configured sensor.
- Input: message with unknown topic.
- Expected: one status publish on `$SYS/opensensemap/error` and reason mentions missing topic.

4. `handle_message_publishes_error_for_non_numeric_value`
- Scenario: inbound value is non-numeric string.
- Input: message value `"abc"`.
- Expected: one status publish on `$SYS/opensensemap/error` with status code `422`.

5. `handle_message_publishes_error_when_sender_throws`
- Scenario: sender callback throws exception.
- Input: message for configured sensor.
- Expected: one status publish on `$SYS/opensensemap/error` with status code `500`.
