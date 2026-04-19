# TODO: publish/qos2/not_authorized_reason

Integration test reference: publish/qos2/not_authorized_reason (spec/integration-test-plan.md section 2.3.7)
Test file: test/integration_tests/publish/core_qos.py

## Problem
Expected PUBREC reason code 0x87 (Not Authorized) for denied QoS2 publish, but broker returned 0x00 (Success).

## Action
fix authorization check integration for inbound QoS2 publish and return PUBREC 0x87 when denied
