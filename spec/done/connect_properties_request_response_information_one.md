# TODO: connect/properties/request_response_information_one

Integration test reference: connect/properties/request_response_information_one (spec/integration-test-plan.md §1.2.8)
Test file: test/integration_tests/connect/connect_properties.py

## Problem
Expected behavior: With Request Response Information = 1, CONNACK contains non-empty Response Information property.
Observed behavior: CONNACK has no Response Information property.

## Action
Implement/fix CONNACK generation to include Response Information when the client requests it in CONNECT.
