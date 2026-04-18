# TODO: connect/properties/maximum_packet_size_enforced

Integration test reference: connect/properties/maximum_packet_size_enforced (spec/integration-test-plan.md §1.2.5)
Test file: test/integration_tests/connect/connect_properties.py

## Problem
Expected behavior: Broker must not send packets larger than client-declared Maximum Packet Size.
Observed behavior: broker still delivers an oversized PUBLISH message.

## Action
Implement/fix outbound packet-size enforcement based on CONNECT Maximum Packet Size and prevent oversized packet transmission.
