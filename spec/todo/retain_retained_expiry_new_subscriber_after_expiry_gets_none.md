# TODO: retain/retained_expiry/new_subscriber_after_expiry_gets_none

Integration test reference: retain/retained_expiry/new_subscriber_after_expiry_gets_none (spec/integration-test-plan.md §4.5.2)
Test file: test/integration_tests/retain/retained_messages.py

## Problem
Expected behavior: a new subscriber connecting after retained message expiry must receive no retained message.
Observed behavior: new subscriber still receives one retained message after expiry timeout.

## Action
fix retained store cleanup or retained lookup filtering by Message Expiry Interval
