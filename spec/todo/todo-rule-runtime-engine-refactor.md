# TODO Plan: Split rule_runtime_engine.cpp into Logical Components

## Scope

`src/yaha/automation_client/rule_runtime_engine.cpp` is 1606 lines and already
flagged by `run_coverage_clients.py` as over the 1000-line non-test file limit.
The file mixes 7 fachlich independent responsibilities behind one anonymous
namespace. This plan splits it into domain-meaningful `.h`/`.cpp` pairs — not
a trivial `_helpers` dump — each representing one real gating/domain concept.

Target location: new sibling directory `src/yaha/automation_client_rule_runtime/`
(next to `automation_client/`, not nested inside it — `automation_client/`
already has 18 code files, above the 10-file-per-directory limit, so new
files must not land there).

## Logical units found in rule_runtime_engine.cpp

1. **Time / weekday gate** (~253 lines) — `readWeekdays`, `weekdayFromTimePoint`,
   `evaluateWeekdayGate`, `parseDurationText`, `parseTimeOfDay`,
   `evaluateRuleStartTime`, `readDurationSeconds`, `evaluateTimeWindowGate`,
   plus local calendar utils (`toLocalCalendarTime`, `localDayStart`,
   `asciiLower`).
2. **Topic filter matching** (~94 lines) — `matchesTopicFilter` (MQTT-style
   glob match), `isMotionTopic`, `isRuleNode`, `topicShapeValid`, `joinPath`.
3. **Rule field access** (~74 lines) — `readNumberField`, `readTopicFilterList`,
   `readTopicFilterArrayOnly`, `fieldIsArray`, `readActiveFlag`.
4. **Event gate evaluation** (anyOf/allOf/noneOf/allow + motion recency,
   ~311 lines) — `collectRecentEventTopics`, `collectRecentMotionTopics`,
   `collectRecentMotionEvents`, `findLatestMotionTimestamp(ForTopic)`,
   `anyEventMatches`, `allFiltersMatchAnyEvent`, `evaluateEventGates`,
   `buildEventTriggerReason`, `collectMatchingTopics`,
   `formatMatchedTopicsWithOptionalTimestamp`, `joinText`, `formatTimeOfDay`.
5. **Event gate trace/explain** (only used by `previewRule`, ~205 lines) —
   `EventGateTraceContext`, `buildEventGateTraceContext`,
   `appendInactivityTraceEntry`, `appendAllOfTraceEntry`,
   `appendAnyOfTraceEntry`, `appendNoneOfTraceEntry`, `appendAllowTraceEntry`,
   `appendConfiguredEventGateTraceEntries`.
6. **Delivery control** (dedup/delay/cooldown, ~179 lines) —
   `valueToStableText`, `isDigitAt`, `isIsoUtcTimestampString`,
   `deliveryComparableHash`, `isZeroPayloadValue`, `readPositiveGateSeconds`,
   `applyDeliveryControls`, `shouldRetainDeliveryStateOnGateMiss`,
   `clearRuleDeliveryState`.
7. **Orchestrator** (stays in `rule_runtime_engine.cpp`, ~420 lines) —
   `RuntimeRuleEvaluationOutcome`, `handleGateMiss`, `handleGateErrors`,
   `appendPathError`, `evaluateRuleNodeRuntime`, `processRuleNode`,
   `processRulesRecursively`, all `RuleRuntimeEngine::` methods.

Dependency direction is acyclic:
Orchestrator -> {Event Gate Trace, Event Gate, Time Gate, Delivery Control} -> {Topic Filter, Field Access}.

## New file layout

`src/yaha/automation_client_rule_runtime/`:

- `rule_runtime_engine.h` / `.cpp` — orchestrator + unchanged public API
  (moved from `automation_client/`)
- `rule_time_gate.h` / `.cpp`
- `rule_topic_filter.h` / `.cpp`
- `rule_field_access.h` / `.cpp`
- `rule_event_gate.h` / `.cpp`
- `rule_event_gate_trace.h` / `.cpp`
- `rule_delivery_control.h` / `.cpp`
- `test/` — moved `rule_runtime_engine_test.cpp` + `TEST_SPEC.md`, plus new
  test files per new module
- `SPEC.md`

## Steps

- [ ] 1. Create `SPEC.md` for `automation_client_rule_runtime/` (purpose,
      public API, module breakdown)
- [ ] 2. Extract `rule_topic_filter.h`/`.cpp` (no internal dependencies)
- [ ] 3. Extract `rule_field_access.h`/`.cpp` (no internal dependencies)
- [ ] 4. Extract `rule_time_gate.h`/`.cpp` (uses `ExpressionEvaluator`/`ExpressionParser`)
- [ ] 5. Extract `rule_event_gate.h`/`.cpp` (uses topic filter + field access)
- [ ] 6. Extract `rule_event_gate_trace.h`/`.cpp` (uses event gate + topic
      filter + field access)
- [ ] 7. Extract `rule_delivery_control.h`/`.cpp` (uses field access)
- [ ] 8. Move `rule_runtime_engine.h`/`.cpp`, reduce to a thin orchestrator
      wired against the new modules
- [ ] 9. Update include in `automation_client_component.h` from
      `yaha/automation_client/rule_runtime_engine.h` to
      `yaha/automation_client_rule_runtime/rule_runtime_engine.h`
- [ ] 10. Update `CMakeLists.txt`: add `src/yaha/automation_client_rule_runtime/*.cpp`
      to the `YAHA_AUTOMATION_SOURCES` glob (~line 118-121); re-check the
      1000-line-limit report
- [ ] 11. Move test file, update `TEST_SPEC.md`; add new isolated unit tests
      per module per `/unit-test` skill (today's 5 `TEST_CASE`s only cover
      the orchestrator level — e.g. `matchesTopicFilter` or
      `evaluateTimeWindowGate` have no isolated unit tests yet)
- [ ] 12. Update `automation_client/SPEC.md` to reference the extracted module
- [ ] 13. Doxygen on all new header declarations
- [ ] 14. Build + coverage: `run_coverage_clients.py --scope src/yaha/automation_client_rule_runtime/`
      and `--scope src/yaha/automation_client/`; target no behavior change,
      coverage >= 80%

## Known follow-up (out of scope here)

`automation_client/` itself stays at 16 code files after step 8 — still
above the 10-file limit, but that is a pre-existing, separate problem not
caused by this refactor. Track separately if it should be addressed.
