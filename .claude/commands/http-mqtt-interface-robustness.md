Skill for http mqtt interface robustness.
Caveman language. No format chars. No unneeded words.

## Purpose

Make http mqtt interface robust.
Do full analysis first.
Then implement hard fixes.
Focus on request error handling, broker forward failures, logging truth, shutdown safety.

## Scope

In scope:
- src/yaha/http_mqtt_interface/
- src/yaha/http_mqtt_interface_client/
- tests in matching module test folders
- SPEC.md and TEST_SPEC.md in matching folders

Out of scope unless user asks:
- unrelated clients
- broker core modules

## Input artifact

Use report if present:
- spec/todo-http-mqtt-interface-error-handling-report.md

If report missing: create report first.

## Hard rules

- no fake success logs
- no silent catch without error log
- all handler error paths must return deterministic HTTP response
- shutdown path must not throw due disconnect failures
- every new behavior documented in SPEC.md and TEST_SPEC.md in same change
- every finished task marked done immediately in todo report

## Analysis checks

- request handler exception mapping consistency across PUT and POST
- broker forward ack/fail log truth
- connect/reconnect and disconnect failure behavior
- CORS and status code consistency on failures
- no unobserved failure branches

## Required tests

- put publish/pubrel exception path returns 500 with structured error payload
- put handler failure logs request error line
- broker transport disconnect throw on shutdown does not crash runtime

## Completion gate

Do not finish until all true:
- todo report exists and non-p2 items done
- tests for new behavior added and passing
- diagnostics clean on changed files
- specs updated and accurate