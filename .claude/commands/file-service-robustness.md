Skill for file service robustness.
Caveman language. No format chars. No unneeded words.

## Purpose

Make file service robust.
Do full analysis first.
Then implement hard fixes.
Focus on error handling, send failure, resend, logging truth, and state consistency.

## Scope

In scope:
- src/yaha/file_store/
- src/yaha/file_store_client/
- src/yaha/mqtt_component/
- src/yaha/mqtt_client/ only if file service flow needs it
- tests in matching module test folders
- SPEC.md and TEST_SPEC.md in matching folders

Out of scope unless user asks:
- broker core modules not used by file service path
- unrelated YAHA clients

## Input artifact

Use report if present:
- spec/todo-file-service-error-handling-report.md

If report missing: run this skill from analysis step and create report first.

## Hard rules

- no fake success logs
- never log outbound as sent before real send success
- no silent catch without error log
- no state divergence between persistent files and observed monitoring state
- every new behavior documented in SPEC.md and TEST_SPEC.md in same change
- every finished task must be marked done immediately in spec/todo-file-service-error-handling-report.md

## Definitions

Sent means publish callback returned success.
Failed send means callback throw or explicit failure return.
Transient failure means disconnect, write fail, ack timeout.
Permanent failure means retry budget exhausted.

## Phase 1 analysis

1) Read files.
- file_store.h/.cpp
- file_store_client_app.h/.cpp
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
- P3 persistence and observation consistency
  file state and monitoring behavior no false success
- P4 callback failure visibility
  callback missing or failure always observable
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

### Step A publish contract usage

Goal: know if monitoring publish succeeded.

Actions:
- consume publish callback result type
- classify failures with category and reason

Acceptance:
- file service can branch on success or failure
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

Goal: transient failures do not lose monitoring messages.

Actions:
- add bounded retry queue
- retry queued messages on next activity cycle
- drop with explicit final error log when budget exhausted

Acceptance:
- transient failure then callback recovery leads to resend attempts
- retry exhaustion logged

### Step D structured error logging

Goal: no silent failure.

Actions:
- replace catch all empty blocks with typed or fallback logs
- add one line structured log per failure branch
- include operation, category, target, reason

Acceptance:
- every non success branch emits one error line

## Required tests

Add or update tests before completion.

File service tests:
- publish callback throw: no false success outbound log
- publish callback explicit failure result path
- retry queue flush after callback recovery
- retry budget exhausted logs explicit failure

Client config tests:
- unchanged unless new config fields added

## Documentation updates

Must update in same change:
- src/yaha/file_store/SPEC.md
- src/yaha/file_store/test/TEST_SPEC.md
- matching file_store_client specs if config or behavior changed

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