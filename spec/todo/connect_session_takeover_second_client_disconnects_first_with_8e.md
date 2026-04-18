# TODO: connect/session_takeover/second_client_disconnects_first_with_8e

Integration test reference: connect/session_takeover/second_client_disconnects_first_with_8e (spec/integration-test-plan.md §1.5.1)
Test file: test/integration_tests/connect/session_takeover.py

## Problem
Second client takeover disconnects the first client, but the observed disconnect reason code is 0x80 instead of expected 0x8E (Session taken over).

## Action
fix session takeover disconnect reason mapping/emission in connection handling
