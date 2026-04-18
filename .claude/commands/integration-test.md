# Integration Test Skill

## ABSOLUTE RULE — NO SOURCE CODE

NEVER read, open, grep, search, or reference any file under src/
NEVER base test expectations on C++ implementation details
Tests verify MQTT 5.0 SPECIFICATION behavior not implementation behavior
Only sources allowed: spec/integration-test-plan.md, spec/anforderungskatalog.md, spec/MQTT Version 5.0.html
Violation produces tests that pass on broken brokers and fail on correct ones

## Framework

Python test modules discovered by `test/run_integration_tests.py`
Broker treated as black-box — communicate only via MQTT protocol
Test plan spec/integration-test-plan.md — always consult before creating tests

## Toolbox (helpers/)

All tests use shared helpers in `test/integration_tests/helpers/`:
- `mqtt_client.py` — paho-mqtt wrapper: connect, disconnect, publish, subscribe, unsubscribe, collect_messages, wait_for_disconnect, will config, topic alias, context manager
- `raw_tcp.py` — raw socket ops: send_bytes, send_partial_connect, open_idle_connection, send_and_expect_close, flood_connections, CONNECT/PUBLISH/generic packet builders
- `assertions.py` — assert_connack, assert_message, assert_reason_code, assert_disconnected, assert_no_message, assert_connection_closed
- `broker.py` — start_broker, stop_broker, restart_broker, is_reachable

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
            assert_connack(sub.connack, reason_code=0x00)
            sub.subscribe("test/topic", qos=1)

            with MqttClient(config.host, config.port, "pub-client") as pub:
                pub.publish("test/topic", b"hello", qos=1)

            msgs = sub.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(msgs[0], topic="test/topic", payload=b"hello", qos=1)
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
python3 test/run_integration_tests.py                        # run all
python3 test/run_integration_tests.py --filter connect       # run category
python3 test/run_integration_tests.py --filter connect/anonymous  # run single
python3 test/run_integration_tests.py --only-failed          # rerun failures
python3 test/run_integration_tests.py --list                 # list all tests
```

Runner auto-builds and starts broker if not running

## Rules

One `.py` file per logical group — multiple TEST_CASES entries per file allowed
Test names must match test plan hierarchy (e.g. `connect/clean_start_new_session`)
No test depends on another test — each test self-contained
Clean up subscriptions and connections within each test — use `with MqttClient(...) as c:` for auto-cleanup
Never hardcode host or port — use `config.host` and `config.port`
Timeouts always from `config.timeout_seconds`
Exception in test function → return `(False, str(e))` — never let exceptions propagate

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
