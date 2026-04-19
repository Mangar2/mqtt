# TODO: connect/connection_errors/malformed_packet_disconnect_81_and_close

Integration test reference: connect/connection_errors/malformed_packet_disconnect_81_and_close (spec/integration-test-plan.md §1.8.3)
Test file: test/integration_tests/connect/connection_errors.py

## Problem
Expected broker behavior for malformed packets is server-initiated DISCONNECT with reason code 0x81 (Malformed Packet) followed by connection close.
Observed behavior is DISCONNECT with reason code 0x82 (Protocol Error).

## Action
fix CONNECT/PUBLISH packet validation error mapping so malformed packet paths return 0x81 instead of 0x82 and keep connection close behavior
