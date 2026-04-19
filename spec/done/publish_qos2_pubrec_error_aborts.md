# TODO: publish/qos2/pubrec_error_aborts

Integration test reference: publish/qos2/pubrec_error_aborts (spec/integration-test-plan.md section 2.3.4)
Test file: test/integration_tests/publish/core_qos.py

## Problem
Expected PUBREC error reason 0x87 and aborted QoS2 flow for denied publish, but broker returned PUBREC 0x00.

## Action
implement authorization-aware QoS2 inbound handling so denied publish yields PUBREC 0x87 and aborts further flow
