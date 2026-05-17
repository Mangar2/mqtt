# Implementation Plan: Automation Client Rule Validation HTTP Interface

This plan defines the implementation sequence for adding a dedicated rule-validation HTTP interface to the Automation Client.
The goal is immediate frontend feedback for rule edits, including exact error localization and clear diagnostics.

## Goal

Implement one HTTP validation surface that supports live frontend editing with debounce and returns:

- validation state (`valid` true/false)
- precise error location (section, source path, line, column)
- stable machine-readable error codes
- user-facing and technical error messages

The existing MQTT management flow (`updated`, `deleted`, `validation_failed`, `persist_failed`) remains fully compatible.

## Scope

In scope:

- single-rule validation endpoint for editor-on-change flow
- full-tree validation endpoint for batch/import scenarios
- structured validation diagnostics including line/column when available
- debounce-friendly response metadata (`requestId`, revision echo, timing)
- lightweight in-memory dedup cache for identical repeated payloads
- tests for validator logic and HTTP contract

Out of scope:

- replacing MQTT rule update flow
- introducing WebSocket push validation
- changing rule execution semantics
- frontend UI implementation details

## Referenced specifications and code anchors

- [SPEC-automation.md](./SPEC-automation.md)
- [src/yaha/automation_client/SPEC.md](../../src/yaha/automation_client/SPEC.md)
- [src/yaha/automation/SPEC.md](../../src/yaha/automation/SPEC.md)
- [src/yaha/error_handling/SPEC.md](../../src/yaha/error_handling/SPEC.md)
- [src/yaha/automation_client/automation_client_component.cpp](../../src/yaha/automation_client/automation_client_component.cpp)
- [src/yaha/automation/rules_tree_json_reader.h](../../src/yaha/automation/rules_tree_json_reader.h)
- [src/yaha/automation/rules_tree_parser.cpp](../../src/yaha/automation/rules_tree_parser.cpp)
- [src/yaha/automation_client/rule_runtime_engine.h](../../src/yaha/automation_client/rule_runtime_engine.h)

## Target architecture

1. Validation domain service (pure logic, no HTTP)
- validates one rule node and full rules tree
- merges JSON read errors, structure errors, and DSL parse errors
- returns unified diagnostic list

2. HTTP adapter layer
- `POST /automation/validate/rule`
- `POST /automation/validate/tree`
- `GET /health`

3. Runtime integration
- optional listener in Automation standalone process
- start/stop tied to component lifecycle
- no dependency on MQTT connection state

## API contract

## Endpoint A: POST /automation/validate/rule

Request body:

```json
{
	"requestId": "f1f4a0f2-8f60-4c6d-9d0f-8de9e2f47201",
	"clientRevision": 128,
	"ruleName": "motion/sleeping",
	"rule": {
		"check": "status/presence = awake",
		"durationWithoutMovementInMinutes": 15,
		"allOf": ["first/hallway/main/motion sensor/detection state"],
		"topic": ["status/presence", "system/presence"],
		"value": "sleeping"
	}
}
```

Response body:

```json
{
	"requestId": "f1f4a0f2-8f60-4c6d-9d0f-8de9e2f47201",
	"clientRevision": 128,
	"valid": false,
	"errors": [
		{
			"code": "YAHA_AUTOMATION_RULE_PARSE_ERROR",
			"severity": "error",
			"message": "unexpected token ')' in expression",
			"userMessage": "Expression has a syntax error.",
			"section": "check",
			"sourcePath": "rules.motion.sleeping.check",
			"line": 1,
			"column": 24,
			"token": ")"
		}
	],
	"warnings": [],
	"latencyMs": 7
}
```

HTTP status rules:

- `200`: request parsed, validation executed (even if invalid)
- `400`: malformed request JSON or missing required top-level fields
- `500`: internal validator/adapter error

## Endpoint B: POST /automation/validate/tree

Request body contains complete rules root object.

Response includes:

- `valid`
- aggregated `errors` with per-rule `sourcePath`
- `errorCount` and optional grouped summary by section

Status rules same as endpoint A.

## Endpoint C: GET /health

Returns `200` with text payload `ok`.

## Diagnostic model

Unified diagnostic item fields:

- `code`: stable machine code
- `severity`: `error` or `warning`
- `message`: technical detail
- `userMessage`: compact UI-facing text
- `section`: one of `rule`, `check`, `value`, `time`, `topic`, `qos`, `events`, `duration`, `json`
- `sourcePath`: full logical path (`rules.<...>.<field>`)
- `line`: 1-based line when known, else omitted
- `column`: 1-based column when known, else omitted
- `token`: optional parser token context

Initial mandatory error codes:

- `YAHA_AUTOMATION_VALIDATE_BAD_REQUEST`
- `YAHA_AUTOMATION_RULE_JSON_PARSE_ERROR`
- `YAHA_AUTOMATION_RULE_STRUCTURE_ERROR`
- `YAHA_AUTOMATION_RULE_PARSE_ERROR`
- `YAHA_AUTOMATION_RULE_FIELD_TYPE_ERROR`
- `YAHA_AUTOMATION_RULE_INTERNAL_ERROR`

## Debounce strategy contract

Frontend requirements:

1. trailing debounce per edited rule: `250..350 ms`
2. max-wait flush: `1000 ms`
3. abort previous inflight validation call on next keystroke
4. apply response only when `requestId` and `clientRevision` match latest sent state

Backend support behavior:

1. echo `requestId` and `clientRevision`
2. include `latencyMs`
3. short-lived dedup cache for identical payload hash (`~1..2 s`) to reduce repeated parse work

## Implementation phases

## Phase 1: Validation result model and error unification

Step 1. Add validation DTO types in Automation Client module
- create `ValidationDiagnostic`, `RuleValidationResult`, `TreeValidationResult`
- keep independent from HTTP and MQTT types

Step 2. Build converters from existing parse/validation outputs
- map JSON read errors (`line`, `column`) to diagnostics
- map expression parse errors (`sourcePath`, token data) to diagnostics
- map structure check failures to diagnostics (replace bool-only blind result)

Deliverable:
- one canonical validation result model usable by MQTT flow and HTTP flow

## Phase 2: Replace bool-only structure validation

Step 3. Extend structure validation API
- evolve [src/yaha/automation_client/rule_runtime_engine.h](../../src/yaha/automation_client/rule_runtime_engine.h) from bool-only shape check to detailed error output
- preserve old wrapper temporarily for backward compatibility

Step 4. Emit section-specific diagnostics
- missing/invalid `topic`
- invalid `qos`
- invalid event gate arrays (`anyOf`, `allOf`, `noneOf`, `allow`)
- invalid numeric/time fields

Deliverable:
- deterministic, path-aware structural diagnostics

## Phase 3: Validator service

Step 5. Implement pure `RuleValidationService`
- inputs: `ruleName`, `ruleNode` or full tree
- outputs: detailed validation result
- no side effects, no persistence, no MQTT publish

Step 6. Ensure path normalization
- normalize reported paths to `rules.<name>.<field>` format
- keep line/column untouched from parser sources

Deliverable:
- reusable validator for HTTP and future tooling

## Phase 4: HTTP adapter

Step 7. Implement HTTP listener adapter for validation endpoints
- endpoint wiring and request/response serialization
- CORS for browser-based editor usage

Step 8. Add strict request guards
- required field checks
- content-type acceptance policy
- deterministic 400 payload on malformed request

Deliverable:
- stable HTTP contract for frontend continuous validation

## Phase 5: Runtime config and lifecycle wiring

Step 9. Extend automation client runtime config
- new keys under `[automationValidationHttp]`:
	- `enabled` (bool, default false)
	- `host` (string, default 127.0.0.1)
	- `port` (uint16, default 0 means disabled)
	- `cacheTtlMs` (uint, optional)

Step 10. Wire startup and shutdown
- start listener in Automation standalone startup path
- stop listener on shutdown signal path
- isolate failures with clear startup error reporting

Deliverable:
- optional, production-safe activation via ini

## Phase 6: Compatibility bridge to current MQTT ack behavior

Step 11. Keep current management ack semantics unchanged
- existing ack payloads remain as-is for MQTT command channel

Step 12. Optionally enrich internal logs with first diagnostic summary
- do not change external MQTT payload contract in this phase

Deliverable:
- zero regression risk for existing automation management clients

## Phase 7: Tests

Step 13. Unit tests for validator service
- valid rule path
- structure failures by section
- expression parse failures with source path
- JSON parse failures with line/column

Step 14. HTTP adapter tests
- request parsing and status codes
- contract JSON shape and field presence
- malformed body and missing field rejection

Step 15. Debounce/race contract tests (integration-light)
- repeated rapid requests with revision increments
- out-of-order response handling using echoed request metadata

Deliverable:
- confidence for live frontend validation under fast typing

## Phase 8: Rollout

Step 16. Ship behind config flag
- disabled by default
- enable only in test/staging first

Step 17. Observe and tune
- track latency distribution
- tune cache TTL and frontend debounce window

Step 18. Enable in production
- only after latency and error-rate acceptance gates are met

Deliverable:
- controlled adoption with clear rollback path (set enabled=false)

## Acceptance criteria

1. Frontend receives validation response for each debounced edit cycle.
2. Invalid rules return at least one error with `section` and `message`.
3. JSON syntax errors include `line` and `column`.
4. Expression errors include `sourcePath` and meaningful parser context.
5. Existing MQTT management ack payloads remain unchanged.
6. Endpoint median latency remains below 30 ms for single-rule validation on local deployment.

## Risks and mitigations

1. Ambiguous line/column mapping for non-JSON structure errors
- mitigation: always include `sourcePath` and `section`; keep line/column optional.

2. Load spikes from aggressive frontend polling
- mitigation: mandatory frontend debounce + backend short TTL dedup cache.

3. Contract drift between frontend and backend
- mitigation: freeze response schema early and validate with adapter tests.

4. Regression in management update path
- mitigation: keep update/ack pipeline unchanged and covered by existing tests.

## Step execution order summary

Step 1 -> Step 2 -> Step 3 -> Step 4 -> Step 5 -> Step 6 -> Step 7 -> Step 8 -> Step 9 -> Step 10 -> Step 11 -> Step 12 -> Step 13 -> Step 14 -> Step 15 -> Step 16 -> Step 17 -> Step 18

