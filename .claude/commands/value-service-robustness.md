Skill for value service robustness.
Caveman language. No format chars. No unneeded words.

## Purpose

Make value service robust.
Do full analysis first.
Then implement hard fixes.
Focus on error handling, send failure, resend, subscriptions, logging truth.

## Scope

In scope:
- src/yaha/value_service/
- src/yaha/value_service_client/
- src/yaha/mqtt_component/
- src/yaha/mqtt_client/ only if needed by value service path
- tests in matching module test folders
- SPEC.md and TEST_SPEC.md in matching folders

Out of scope unless user asks:
- unrelated YAHA clients
- broker core modules not used by value service

## Input artifact

Use report if present:
- spec/todo-value-service-error-handling-report.md

If report missing: run this skill from analysis step and create report first.

## Hard rules

- no fake success logs
- never log outbound as sent before real send success
- no silent catch without error log
- subscriptions must reflect active key map exactly
- every new behavior documented in SPEC.md and TEST_SPEC.md in same change
- every finished task must be marked done immediately in spec/todo-value-service-error-handling-report.md

## Definitions

Sent means publish callback returned success.
Failed send means callback throw or explicit failure return.
Transient failure means disconnect, write fail, ack timeout.
Permanent failure means retry budget exhausted.

## Phase 1 analysis

1) Read files.
- value_service_component.h/.cpp
- value_service_client_app.h/.cpp
- matching SPEC.md TEST_SPEC.md

2) Build failure map.
For each path list:
- trigger
- current handling
- current logging
- lost state risk
- retry behavior

3) Validate five critical properties.
- P1 logging truth
  outbound log only after send success
- P2 resend policy
  transient send failure retried
- P3 subscription truth
  key map and <key>/set subscriptions in sync
- P4 persistence observability
  file store get/post failures visible
- P5 observability
  each failure class has machine parseable log

4) Write TODO report in spec/.
Must include:
- severity ordered findings P0 P1 P2
- exact file and line evidence
- implementation tasks with acceptance criteria
- test extension tasks

Stop phase 1 only when all five properties are evaluated.

## Phase 2 implementation

### Step A publish result handling

Goal: know if publish succeeded.

Actions:
- use callback result category and reason
- classify callback exceptions

Acceptance:
- value service can branch on success or failure
- no exception swallowing without structured log

### Step B truthful outbound logging

Goal: outbound logs reflect real send.

Actions:
- move success log after confirmed send
- add failure log line on send failure
- include topic qos retain category reason

Acceptance:
- failed send never emits success outbound line
- tests prove ordering

### Step C resend policy

Goal: transient failures do not lose retained state publishes.

Actions:
- add bounded retry queue
- retry queued publishes on later activity
- log retry exhausted and drop

Acceptance:
- callback recovery flushes queued publishes
- retry exhaustion logged

### Step D structured error logging

Goal: no silent failure.

Actions:
- log FileStore GET/POST failures with operation and reason
- log monitor reload failures

Acceptance:
- each non success branch emits one structured line

## Required tests

Add or update tests before completion.

Value service tests:
- publish callback throw no false success log
- publish callback explicit failure category log
- retry queue flush after callback restore
- retry budget exhausted log

Value service client tests:
- unchanged unless config keys changed

## Documentation updates

Must update in same change:
- src/yaha/value_service/SPEC.md
- src/yaha/value_service/test/TEST_SPEC.md
- matching value_service_client specs if config changed

README update needed if config keys added for retry policy.

## Completion gate

Do not finish until all true:
- TODO report exists in spec/ and reflects final code
- all new behavior has tests
- changed tests pass
- no IDE diagnostics in changed files
- SPEC.md TEST_SPEC.md updated and accurate
- logs show truthful outbound semantics
- every TODO item that is not in section P2 Nice To Have is implemented, tested, and marked done in the TODO report

## Execution style

Small safe commits.
One robustness feature at a time.
After each feature:
- run targeted tests
- recheck logs and acceptance
- proceed to next feature

No workaround.
No partial fake fix.