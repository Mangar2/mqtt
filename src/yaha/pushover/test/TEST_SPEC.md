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
- Expected: one status publish on `$SYS/pushover/error` with status code `500`.

5. `handle_message_publishes_error_when_no_device_is_configured`
- Scenario: component has no configured devices.
- Input: message for configured subscription.
- Expected: status publish on `$SYS/pushover/error` with status code `422`.
