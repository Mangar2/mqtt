# TODO: robustness/connection_abuse_idle_tcp_timeout

Integration test reference: robustness/connection_abuse_idle_tcp_timeout (spec/integration-test-plan.md §19.2.1)
Test file: test/integration_tests/robustness/connection_abuse.py

## Problem
Expected broker to close an idle TCP connection with no CONNECT data within the 30-second abuse window. Current behavior keeps the TCP connection open beyond the test window.

## Action
implement pre-CONNECT idle connection timeout handling in broker connection lifecycle
