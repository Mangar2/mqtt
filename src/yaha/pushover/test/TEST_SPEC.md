# Pushover Component Test Specification

## Test cases

1. `subscriptions_include_all_configured_topic_filters`
- Scenario: component initialized with two configured subscriptions.
- Input: `getSubscriptions()`.
- Expected: map contains both topics with configured qos.

2. `handle_message_posts_to_each_device_and_publishes_success_status`
- Scenario: sender succeeds for all devices.
- Input: message with `alert` value and one reason.
- Expected: one sender call and one success status publish per configured device.

3. `handle_message_uses_default_priority_for_non_alert_values`
- Scenario: message value is not `alert`.
- Input: message with value `warning`.
- Expected: outgoing payload contains `"priority":-1`.

4. `handle_message_publishes_error_when_sender_throws`
- Scenario: sender callback throws exception.
- Input: message for configured subscription.
- Expected: one status publish on `$MONITOR/pushover/error` with status code `500`.

5. `handle_message_publishes_error_when_no_device_is_configured`
- Scenario: component has no configured devices.
- Input: message for configured subscription.
- Expected: status publish on `$MONITOR/pushover/error` with status code `422`.

6. `handle_message_reports_error_when_publish_callback_missing` (test file: `handle_message_publishes_error_when_sender_callback_missing`)
- Scenario: incoming message arrives without publish callback.
- Input: message for configured subscription.
- Expected: processing fails with callback-missing reason, and `logError` emits a structured
  stderr line via shared `buildMessageLogLine` (`pushover <- <topic> :`, asserted via
  `yaha::test::messageLogLinePrefix`, plus `reason="..."`).

7. `handle_message_reports_unknown_exception_from_sender`
- Scenario: sender throws non-std exception.
- Input: message for configured subscription.
- Expected: unknown exception reported via error status topic.

8. `handle_message_parses_error_reason_from_response_payload` (test file: `handle_message_formats_error_payload_arrays_for_http_failure`)
- Scenario: sender returns non-success response with JSON reason payload.
- Input: message for configured subscription.
- Expected: published reason contains parsed remote error text, and `logHttpError` emits a
  structured stderr line via shared `buildMessageLogLine` (`pushover <- <topic> :`, asserted via
  `yaha::test::messageLogLinePrefix`, plus `httpStatus=500`).

9. `handle_message_ignores_input_when_not_running`
- Scenario: message handled before run-state.
- Input: valid message.
- Expected: no sender or publish callback activity.

10. `close_without_run_is_safe`
- Scenario: close called without prior run.
- Input: lifecycle close call.
- Expected: no side effects and no publish actions.

11. `handle_message_without_publish_callback_does_not_throw`
- Scenario: component handles input while publish callback is unset.
- Input: valid subscription message while running.
- Expected: no exception and internal callback-missing branch is exercised.

12. `handle_message_logs_status_publish_failure_path`
- Scenario: publish callback returns failure for status messages.
- Input: sender returns HTTP error and callback returns `PublishResult::fail`.
- Expected: no exception and status-publish-failure branch is exercised.
