# TODO: publish/qos1/no_matching_subscribers_reason

Integration test reference: publish/qos1/no_matching_subscribers_reason (spec/integration-test-plan.md section 2.2.5)
Test file: test/integration_tests/publish/core_qos.py

## Problem
Expected PUBACK reason code 0x10 (No matching subscribers) for QoS1 publish without subscribers, but broker returned 0x00 (Success).

## Action
implement QoS1 no-subscriber reason code handling in publish acknowledgment path
