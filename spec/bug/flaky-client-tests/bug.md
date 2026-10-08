# Flaky client tests

## user report

korrigiere den anderen fremden test oder den code wenn der test korrekt ist aber der code nicht. alle tests sollten grün werden

## context

Two full client runs (`python3 test/run_coverage_clients.py`) on 2026-10-08 failed in different tests:

- `serial_device_runtime_internal_trace_logs_keepalive_and_status_reply_flow`: SIGABRT during coverage execution phase
  (`src/yaha/serial_device/test/serial_device_runtime_oracle_test.cpp:423`)
- `value_service_monitoring_filesystem_watch_is_ignored`: exceeded the 2s per-test timeout during unit phase

Both tests passed in other runs, so failures are intermittent.

## test case

Standard suite, full client run:

```sh
python3 test/run_coverage_clients.py
```

## scope

Allow: the two named tests and the production code they exercise.
Deny: all other tests and modules.
