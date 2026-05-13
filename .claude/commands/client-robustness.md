Skill for automation client robustness.
Caveman language. No format chars. No unneeded words.

## Purpose

Make automation client robust.
Do full analysis first.
Then implement hard fixes.
Focus on error handling, reconnect flow, send failure, resend, subscription sync, logging truth.

## Scope

In scope:
- src/yaha/automation_client/
- src/yaha/mqtt_client/
- src/yaha/mqtt_component/
- tests in matching module test folders
- SPEC.md and TEST_SPEC.md in matching folders

Out of scope unless user asks:
- broker core modules not used by mqtt client adapter
- deployment scripts
- unrelated YAHA clients

## Input artifact

Use report if present:
- spec/todo-automation-client-error-handling-report.md

If report missing: run this skill from analysis step and create report first.

## Hard rules

- no fake success logs
- never log outbound as sent before real send success
- no silent catch without error log
- no state divergence between in memory rules and filestore rules
- subscriptions must represent broker confirmed state
- reconnect must not drop automation messages without explicit policy
- every new behavior documented in SPEC.md and TEST_SPEC.md in same change
- every finished task must be marked done immediately in spec/todo-automation-client-error-handling-report.md

## Definitions

Sent means publish callback returned success.
Failed send means callback throw or explicit failure return.
Transient failure means disconnect, write fail, ack timeout.
Permanent failure means validation error or retry budget exhausted.

## Phase 1 analysis

1) Read files.
- automation_client_component.h/.cpp
- mqtt_component.h
- mqtt_client.h/.cpp
- broker_transport.cpp
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
- P3 transactional rule persistence
  update delete no divergence
- P4 subscription truth
  active map equals broker confirmed map
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

### Step A publish contract

Goal: know if publish succeeded.

Actions:
- change publish callback contract from void to result type
- include error category for failure
- adapt mqtt client injection path
- adapt automation component publish call sites

Acceptance:
- automation component can branch on success or failure
- no exception swallowing without classified log

### Step B truthful outbound logging

Goal: outbound logs reflect real send.

Actions:
- move success log after confirmed send
- add failure log line on send failure
- include topic qos retain failure category reason

Acceptance:
- failed send never emits success outbound line
- test proves ordering

### Step C resend policy

Goal: transient failures do not lose automation output.

Actions:
- add bounded retry queue in automation component or dedicated helper
- add retry backoff and max attempts
- retry only transient failure classes
- drop with explicit final error log when budget exhausted

Acceptance:
- transient failure then reconnect leads to resend attempts
- permanent failures not retried

### Step D transactional rule update delete

Goal: avoid state divergence.

Actions:
- stage change
- persist staged payload first or rollback on fail
- commit in memory rules and dynamic subscriptions only on successful persist
- ack payload must distinguish validation_failed persist_failed publish_failed deleted updated

Acceptance:
- filestore fail does not leave changed in memory state
- update and delete both return deterministic ack

### Step E subscription truth model

Goal: runtime subscription state equals broker confirmed state.

Actions:
- track desired subscriptions and confirmed subscriptions separately
- apply diff against confirmed
- update confirmed only after suback unsuback success
- reconnect replay from desired set with confirmation update

Acceptance:
- diagnostics can show desired vs pending vs confirmed
- no optimistic active map overwrite

### Step F structured error logging

Goal: no silent failure.

Actions:
- replace catch all empty blocks with typed or fallback logs
- add one line structured log per failure branch
- include operation, category, target, reason

Acceptance:
- every non success branch emits one error line

## Required tests

Add or update tests before completion.

Automation component tests:
- filestore get fail startup and monitor reload
- filestore post fail update rollback
- filestore post fail delete rollback
- publish failure no false success log
- resend queue retries and budget exhaust

MQTT client transport tests:
- qos1 ack timeout mapped to transient failure
- qos2 ack timeout mapped to transient failure
- subscribe unsubscribe confirmation state model
- reconnect plus resend integration path

Logging tests:
- outbound success line only after confirmed send
- failure line emitted on each failure class

## Documentation updates

Must update in same change:
- src/yaha/automation_client/SPEC.md
- src/yaha/mqtt_client/SPEC.md
- src/yaha/mqtt_component/SPEC.md
- matching TEST_SPEC.md files

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
