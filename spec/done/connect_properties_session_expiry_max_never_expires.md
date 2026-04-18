# TODO: connect/properties/session_expiry_max_never_expires

Integration test reference: connect/properties/session_expiry_max_never_expires (spec/integration-test-plan.md §1.2.3)
Test file: test/integration_tests/connect/connect_properties.py

## Problem
Expected behavior: Session Expiry Interval = 0xFFFFFFFF keeps session state and queued QoS1 message after reconnect.
Observed behavior: queued message is not delivered after reconnect (timeout waiting for message).

## Action
Implement/fix never-expiring session handling and persisted offline QoS1 delivery for Session Expiry Interval 0xFFFFFFFF.
