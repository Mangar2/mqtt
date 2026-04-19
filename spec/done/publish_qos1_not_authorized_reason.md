# TODO: publish/qos1/not_authorized_reason

Integration test reference: publish/qos1/not_authorized_reason (spec/integration-test-plan.md section 2.2.6)
Test file: test/integration_tests/publish/core_qos.py

## Problem
Expected PUBACK reason code 0x87 (Not Authorized) for denied QoS1 publish, but broker returned 0x00 (Success).

## Action
fix authorization check integration for inbound QoS1 publish and return PUBACK 0x87 when denied
