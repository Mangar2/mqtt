# TODO: robustness/connection_abuse_partial_connect_timeout

Integration test reference: robustness/connection_abuse_partial_connect_timeout (spec/integration-test-plan.md §19.2.2)
Test file: test/integration_tests/robustness/connection_abuse.py

## Problem
Expected broker to timeout and close a connection after receiving only a partial CONNECT packet. Current behavior keeps the connection open past the timeout window instead of closing it.

## Action
fix partial CONNECT read timeout enforcement for incomplete handshake state
