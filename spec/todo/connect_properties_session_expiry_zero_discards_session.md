# TODO: connect/properties/session_expiry_zero_discards_session

Integration test reference: connect/properties/session_expiry_zero_discards_session (spec/integration-test-plan.md §1.2.1)
Test file: test/integration_tests/connect/connect_properties.py

## Problem
Expected behavior: Session Expiry Interval = 0 discards the session on disconnect and reconnect with the same Client ID returns Session Present = 0.
Observed behavior: broker returns Session Present = 1.

## Action
Implement/fix CONNECT Session Expiry handling for value 0 so the session is removed on disconnect and not resumed on reconnect.
