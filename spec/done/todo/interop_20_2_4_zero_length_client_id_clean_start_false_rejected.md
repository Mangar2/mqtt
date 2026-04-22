# TODO: interop/20_2_4_zero_length_client_id_clean_start_false_rejected

Integration test reference: interop/20_2_4_zero_length_client_id_clean_start_false_rejected (spec/integration-test-plan.md §20.2.4)
Test file: test/integration_tests/interop/edge_cases_from_spec.py

## Problem

Broker accepts a CONNECT with zero-length ClientID and CleanStart=0, returning
CONNACK 0x00 (success). MQTT 5.0 §3.1.3.1 states that a Client supplying a
zero-byte ClientID MUST set CleanStart to 1; the broker MUST respond with
CONNACK 0x85 (Client Identifier not valid) and close the connection when
CleanStart=0 is combined with an empty ClientID.

## Action

fix CONNECT handler — when CleanStart flag is 0 and the received ClientID has
zero length, reject with CONNACK reason code 0x85 (Client Identifier not valid)
and close the connection.
