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

Repro (repeats each test as its own process and flags exit != 0 or > 2s):

```sh
python3 spec/bug/flaky-client-tests/repro.py 100
```

Result before fix (coverage build):

- value_service test: 6/100 runs hang (killed after 10s)
- serial_device test: 55/100 runs fail (SIGABRT or `find(...) != npos` assertion)

## root cause

value_service test (test defect):
`reserveFreeLocalPort()` releases the port, then the mock starts `listen()` asynchronously while
`waitForHttpServer()` already connects. Without a listener a localhost client can self-connect
(source port == destination port); the connection is established, no one answers, and the
health GET without read timeout waits forever. The mock listen then fails because the port is
taken. Stack sample: main thread blocked in `waitForHttpServer -> Client::Get -> select_read`,
no listen thread alive.

serial_device test (test defect):
`std::cout` was redirected into a plain `std::ostringstream` while the component keepalive
thread and the test thread log concurrently, a data race that aborts or loses lines. The fixed
10 ms wait also often ended before the first keepalive `at` was logged under coverage.
Production code logs to the real `std::cout`, which tolerates concurrent writers.

## fix

- value_service mock binds its port synchronously (`bind_to_port` + `listen_after_bind`);
  health check uses connection and read timeouts.
- serial_device test captures through a mutex-protected streambuf and polls for the expected
  log lines (bounded) instead of a fixed 10 ms sleep.

## scope

Allow: the two named tests and the production code they exercise.
Deny: all other tests and modules.

## result

- `python3 test/run_coverage_clients.py`: status=ok, threshold met
- `python3 spec/bug/flaky-client-tests/repro.py 200`: 0/200 failures for both tests (PASS)
- First fix round left 4/100 serial failures: trace lines were written with several `<<` calls, so
  the keepalive thread could interleave inside a line (production defect, also visible in the
  journal). Each trace line is now built first and written with one stream call.
