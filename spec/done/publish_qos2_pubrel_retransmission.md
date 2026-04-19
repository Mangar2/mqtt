# TODO: publish/qos2/pubrel_retransmission

Integration test reference: publish/qos2/pubrel_retransmission (spec/integration-test-plan.md section 2.3.5)
Test file: test/integration_tests/publish/core_qos.py

## Problem
Expected broker to accept PUBREL retransmission after completed PUBREC stage and respond with PUBCOMP again, but broker sent DISCONNECT (packet type 14).

## Action
fix QoS2 state machine handling for duplicate PUBREL so retransmissions are idempotently acknowledged with PUBCOMP
