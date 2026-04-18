# Integration Test Skill

## Framework

Python test modules discovered by `test/run_integration_tests.py`
Broker treated as black-box — communicate only via MQTT protocol
Test plan spec/integration-test-plan.md — always consult before creating tests

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

## MQTT client tools

Prefer `mqttx` CLI for simple pub/sub tests
Use `paho-mqtt` Python library for complex flows (QoS 2 handshake, will messages, session resume, properties)
Use raw TCP sockets only for malformed packet / robustness tests
Always set `--reconnect-period 0` with mqttx to avoid auto-reconnect

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
Clean up subscriptions and connections within each test
Handle `FileNotFoundError` for missing CLI tools
Handle `subprocess.TimeoutExpired`
Never hardcode port — use `config.port`
