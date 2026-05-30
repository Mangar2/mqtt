# OpenSenseMap Component Test Specification

## Test cases

1. `subscriptions_include_all_configured_sensor_topics`
- Scenario: component initialized with two configured sensors.
- Input: `getSubscriptions()`.
- Expected: map contains both sensor topics with configured qos.

2. `handle_message_publishes_success_status_for_http_201`
- Scenario: sender returns HTTP 201 with JSON message payload.
- Input: numeric message for configured sensor.
- Expected: one status publish on `$MONITOR/opensensemap/success`, value `201`, reason contains extracted JSON message.

3. `handle_message_publishes_error_when_sensor_mapping_missing`
- Scenario: inbound topic has no configured sensor.
- Input: message with unknown topic.
- Expected: one status publish on `$MONITOR/opensensemap/error` and reason mentions missing topic.

4. `handle_message_publishes_error_for_non_numeric_value`
- Scenario: inbound value is non-numeric string.
- Input: message value `"abc"`.
- Expected: one status publish on `$MONITOR/opensensemap/error` with status code `422`.

5. `handle_message_publishes_error_when_sender_throws`
- Scenario: sender callback throws exception.
- Input: message for configured sensor.
- Expected: one status publish on `$MONITOR/opensensemap/error` with status code `500`.

6. `handle_message_reports_error_when_publish_callback_missing`
- Scenario: incoming message arrives without configured publish callback.
- Input: message for known sensor.
- Expected: internal handling fails gracefully with callback-missing reason.

7. `handle_message_reports_error_on_unknown_exception`
- Scenario: sender callback throws a non-std exception type.
- Input: message for known sensor.
- Expected: status/error publish reports unknown exception.

8. `handle_message_ignores_input_when_not_running`
- Scenario: message handled before component run-state is active.
- Input: message for known sensor.
- Expected: no request sender call and no publish callback invocation.

9. `close_without_run_keeps_component_inactive`
- Scenario: close called without prior run.
- Input: lifecycle close call.
- Expected: close is idempotent and does not publish/execute requests.

10. `handle_message_without_publish_callback_does_not_throw`
- Scenario: component handles message without configured publish callback.
- Input: valid sensor message while running.
- Expected: no exception and request path executes safely.

11. `handle_message_logs_status_publish_failure_path`
- Scenario: publish callback returns failure while status message is emitted.
- Input: sender returns HTTP 500 and callback returns `PublishResult::fail`.
- Expected: no exception and status-publish-failure branch is exercised.
