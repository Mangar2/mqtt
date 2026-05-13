Skill for message store service robustness.
Caveman language. No format chars. No unneeded words.

## Purpose

Make message store service robust.
Do full analysis first.
Then implement hard fixes.
Focus on persistence failure visibility, http runtime failures, config error handling, logging truth.

## Scope

In scope:
- src/yaha/message_store/
- src/yaha/message_store_client/
- tests in matching folders
- SPEC.md and TEST_SPEC.md in same module folders

Out of scope unless user asks:
- unrelated YAHA clients
- broker core modules

## Input artifact

Use report if present:
- spec/todo-message-store-service-error-handling-report.md

If report missing: create report first.

## Hard rules

- no silent failure branch
- no fake success logs
- persistence restore and final persist failures must be observable
- runtime config parser behavior must match documented constraints
- every finished task marked done immediately in TODO report

## Required tests

- restore failure branch emits structured error log
- final persist failure branch emits structured error log
- tree.lengthForFurtherCompression config accepts 0 as documented

## Completion gate

Do not finish until all true:
- TODO report exists and non-P2 items are done
- new tests pass
- diagnostics clean on changed files
- specs updated and accurate