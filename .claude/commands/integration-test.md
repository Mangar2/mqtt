# Integration Test Skill

## ABSOLUTE RULE — NO SOURCE CODE

NEVER read, open, grep, search, or reference any file under src/
NEVER base test expectations on C++ implementation details
Tests verify MQTT 5.0 SPECIFICATION behavior not implementation behavior
Only sources allowed: spec/integration-test-plan.md, spec/anforderungskatalog.md, spec/MQTT Version 5.0.html
Violation produces tests that pass on broken brokers and fail on correct ones
Never run all integration tests, run only integration test belonging to the project you work on

## Framework

Python test modules discovered by `test/run_integration_tests.py`
Broker treated as black-box — communicate only via MQTT protocol
Test plan spec/integration-test-plan.md — always consult before creating tests

Integration tests are split into independent domains with separate runners:

- Broker + MQTT test client domain:
    - tests under `test/integration_tests/`
    - runner: `test/run_integration_tests.py`
    - includes thin client shell bridge module `test/integration_tests/client/test_client_shell.py`
        that exposes shared cases from `test/integration_client_shell_cases.py`
- YAHA client domain (component-specific integration tests):
    - tests under `test/yaha/<client_name>/`
    - runner: `test/yaha/run_msgstore_integration_tests.py`
    - despite script name, this runner discovers all modules under `test/yaha/`

Never mix domains in one test file.
If task is about a YAHA client (for example ValueService), create and run tests only in the YAHA client domain.

## Toolbox (helpers/)

All tests use shared helpers in `test/integration_tests/helpers/`:
- `mqtt_client.py` — paho-mqtt wrapper: connect, disconnect, publish, subscribe, unsubscribe, collect_messages, wait_for_disconnect, will config, topic alias, context manager
- `raw_tcp.py` — raw socket ops: send_bytes, send_partial_connect, open_idle_connection, send_and_expect_close, flood_connections, CONNECT/PUBLISH/generic packet builders
- `assertions.py` — assert_connack, assert_message, assert_reason_code, assert_disconnected, assert_no_message, assert_connection_closed
- `broker.py` — start_broker, stop_broker, restart_broker, is_reachable

RULE: Before writing any test, read `test/integration_tests/helpers/assertions.py` to verify exact function signatures. All parameters are positional and required — no defaults exist.

Import helpers:
```python
from helpers.mqtt_client import MqttClient
from helpers.raw_tcp import send_bytes, send_and_expect_close
from helpers.assertions import assert_connack, assert_message, assert_no_message
```

## File location

```
test/integration_tests/<category>/<test_name>.py
```

Category = subdirectory matching test plan section (connect, publish, subscribe, retain, will, session, qos, topic, websocket, auth, acl, monitoring, shutdown, load, robustness, interop)

YAHA client integration file location:

```
test/yaha/<client_name>/<test_name>.py
```

Examples for `<client_name>`: `msgstore_client`, `broker_connector_client`, `value_service`.

## Test module structure

Every `.py` file exports `TEST_CASES` list. Each entry dict with three keys:

```python
TEST_CASES = [
    {
        "name": "category/test_name",       # hierarchical slash-separated name
        "description": "Short English description",
        "run": function_reference,           # callable(config) -> tuple[bool, str]
    }
]
```

Order rule for `TEST_CASES`:
- If descriptions start with requirement numbers (for example `1.2.3`), entries must be ordered by numeric requirement order (1.2.9 before 1.2.10)
- Never append a new numbered case at the end when its number belongs in the middle — insert at the correct numeric position
- Unnumbered cases come after numbered cases and are ordered by `name`
- Keep this order stable for all edits to avoid noisy diffs and list output jumps

## Test function signature

```python
def my_test(config) -> tuple[bool, str]:
```

`config` has: `config.host` (str), `config.port` (int), `config.timeout_seconds` (float)
Return `(True, "details")` on success, `(False, "reason")` on failure

## When to use which tool

Use `MqttClient` (paho-mqtt wrapper) for:
- All QoS 1/2 flows, will messages, session resume, properties, subscription options, topic alias, retained messages, shared subscriptions, enhanced auth, flow control

Use `raw_tcp` for:
- Malformed packets, garbage bytes, truncated data, invalid headers, half-open connections, protocol version errors, flood/stress connections

Use `mqttx` CLI only for:
- Simplest smoke tests (anonymous connect, basic QoS 0 pub). Avoid for anything requiring property inspection or multi-step flows

## Example: paho-mqtt based test

```python
from helpers.mqtt_client import MqttClient
from helpers.assertions import assert_connack, assert_message

def run_qos1_roundtrip(config) -> tuple[bool, str]:
    try:
        with MqttClient(config.host, config.port, "sub-client") as sub:
            assert_connack(sub.connack, reason_code=0x00, session_present=False)
            sub.subscribe("test/topic", qos=1)

            with MqttClient(config.host, config.port, "pub-client") as pub:
                pub.publish("test/topic", b"hello", qos=1)

            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic="test/topic", payload=b"hello", qos=1, retain=False)
        return True, "QoS 1 roundtrip OK"
    except Exception as e:
        return False, str(e)
```

## Example: raw TCP test

```python
from helpers.raw_tcp import send_and_expect_close

def run_garbage_bytes(config) -> tuple[bool, str]:
    try:
        closed = send_and_expect_close(config.host, config.port, b"\xff\x00\xde\xad", timeout=5.0)
        if not closed:
            return False, "broker did not close connection on garbage input"
        return True, "broker closed connection on garbage bytes"
    except Exception as e:
        return False, str(e)
```

## Running tests

All commands from project root:

```
python3 test/run_integration_tests.py --filter connect                     # broker domain category
python3 test/run_integration_tests.py --filter connect/anonymous           # broker domain single test
python3 test/run_integration_tests.py --filter client/test_client_shell    # mqtt test client integration domain
python3 test/run_integration_tests.py --only-failed                        # rerun failed broker/client tests
python3 test/run_integration_tests.py --list                               # list broker/client tests

python3 test/yaha/run_msgstore_integration_tests.py --filter yaha/msgstore
python3 test/yaha/run_msgstore_integration_tests.py --filter broker_connector/
python3 test/yaha/run_msgstore_integration_tests.py --list
```

Runner auto-builds and starts broker if not running

Rule: never run all integration tests when working on one scope.
Always use `--filter` for the current project scope.

## Creating new YAHA client integration tests

Use this workflow when asked to add tests for YAHA clients (for example ValueService):

1. Create or reuse client directory under `test/yaha/<client_name>/`.
2. Add one Python module with one or more `TEST_CASES` entries.
3. Use test names with client prefix, for example `yaha/value_service/<case_name>`.
4. Implement each run function with signature `def run_xxx(config) -> tuple[bool, str]`.
5. Reuse helpers from `test/integration_tests/helpers/` by dynamic import pattern used in existing YAHA tests.
6. Build and manage required client process/broker process inside the test case setup/teardown.
7. Execute only scoped tests via:

```sh
python3 test/yaha/run_msgstore_integration_tests.py --filter yaha/value_service
```

Minimal module skeleton:

```python
from __future__ import annotations

def run_value_service_smoke(config) -> tuple[bool, str]:
    try:
        # setup runtime, publish/assert, teardown
        return True, "value service integration smoke passed"
    except Exception as error:
        return False, str(error)


TEST_CASES = [
    {
        "name": "yaha/value_service/smoke",
        "description": "ValueService integration smoke test.",
        "run": run_value_service_smoke,
    }
]
```

## Rules

One `.py` file per logical group — multiple TEST_CASES entries per file allowed
Test names must match test plan hierarchy (e.g. `connect/clean_start_new_session`)
No test depends on another test — each test self-contained
Clean up subscriptions and connections within each test — use `with MqttClient(...) as c:` for auto-cleanup
Never hardcode host or port — use `config.host` and `config.port`
Timeouts always from `config.timeout_seconds`
Exception in test function → return `(False, str(e))` — never let exceptions propagate

## Defect visibility rule

Integration tests must detect defects, never hide them.
Do not relax assertions, expected reason codes, protocol checks, or timing expectations only to match current broker behavior.
If broker behavior differs from spec expectation, keep spec-based assertion and let the test fail.
Record the failure as implementation work (TODO) instead of weakening the test.
If it is unclear whether a mismatch should be solved by changing the test or fixing the broker,
read the MQTT 5.0 specification first (`spec/MQTT Version 5.0.html`) and decide from normative rules.
Only change the test when the test expectation is not specification-conformant.
Fix the broker when implementation behavior is not specification-conformant.

## Failed tests → create TODO file

When an integration test fails and the failure indicates a broker defect or missing feature:
Create a file in `spec/todo/` named after the test (slashes replaced by underscores), e.g.:

```
spec/todo/connect_clean_start_resume_session.md
```

Content:

```markdown
# TODO: connect/clean_start_resume_session

Integration test reference: connect/clean_start_resume_session (spec/integration-test-plan.md §1.4.3)
Test file: test/integration_tests/connect/clean_start.py

## Problem
<Short description of failure: what was expected vs what happened>

## Action
<implement | fix> <affected module/component>
```

One file per failed test. Delete the TODO file after the fix is verified.
