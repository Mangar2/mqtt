# TODO: connect/properties/session_expiry_positive_persists_session

Integration test reference: connect/properties/session_expiry_positive_persists_session (spec/integration-test-plan.md §1.2.2)
Test file: test/integration_tests/connect/connect_properties.py

## Problem
Expected behavior: Session Expiry Interval > 0 keeps the session and queued QoS1 message across disconnect/reconnect.
Observed behavior: reconnect does not deliver the queued message (timeout waiting for message).

## Action
Implement/fix durable session persistence and offline QoS1 queue delivery when Session Expiry Interval is non-zero.
