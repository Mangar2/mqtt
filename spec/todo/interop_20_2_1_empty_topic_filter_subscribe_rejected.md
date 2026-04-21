# TODO: interop/20_2_1_empty_topic_filter_subscribe_rejected

Integration test reference: interop/20_2_1_empty_topic_filter_subscribe_rejected (spec/integration-test-plan.md §20.2.1)
Test file: test/integration_tests/interop/edge_cases_from_spec.py

## Problem

Broker returns SUBACK with reason code 0x00 (success) for a SUBSCRIBE containing
an empty topic filter. MQTT 5.0 §4.7.3 forbids empty topic filters and requires
the broker to treat this as a Protocol Error (reason code 0x82). The broker must
either respond with SUBACK 0x82 per-filter or send DISCONNECT 0x82 and close the
connection.

## Action

fix subscribe handler — validate that each topic filter in a SUBSCRIBE payload is
non-empty; return SUBACK reason code 0x82 (Protocol Error) for any empty filter or
send DISCONNECT 0x82 and close the connection.
