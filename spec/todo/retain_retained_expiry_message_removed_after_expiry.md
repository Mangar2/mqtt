# TODO: retain/retained_expiry/message_removed_after_expiry

Integration test reference: retain/retained_expiry/message_removed_after_expiry (spec/integration-test-plan.md §4.5.1)
Test file: test/integration_tests/retain/retained_messages.py

## Problem
Expected behavior: a retained message published with Message Expiry Interval must be removed after expiry and must not be delivered to subscribers after expiry.
Observed behavior: subscriber still receives one retained message after expiry timeout.

## Action
implement retained message expiry enforcement in retained-message delivery path
