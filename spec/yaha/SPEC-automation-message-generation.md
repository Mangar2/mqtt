# Automation – Message Generation Flow

This document describes the original JavaScript automation rule engine's
message generation flow and compares it with the C++ reimplementation.

Source references:
- `spec/@mangar2/automation/automation.js`
- `spec/@mangar2/rules/processrule.js`
- `spec/@mangar2/rules/rulehistory.js`
- `spec/@mangar2/rules/checkevents.js`
- `spec/@mangar2/automation/eventhistory.js`
- `src/yaha/automation_client/rule_runtime_engine.cpp`

---

## Part 1 – Original JavaScript Flow

### 1. Entry points

There are two entry points that trigger rule evaluation:

**A. `handleMessage(message)`**

Called for every incoming MQTT message.
1. Route message: simulation / management / domain.
2. For domain messages: add to event history (`addEvent`) and update variable map (`setVariable`).
3. Call `processTasks(now)` — evaluate all rules immediately.
4. Return `[ruleMessages, responseMessages]`.

**B. `processTasks(date, simulation)`**

Called periodically (every `intervalInSeconds`) and by `handleMessage`.
1. Set current date on `ProcessRule`.
2. Get `recentEvents` from event history: latest motion timestamp, motion event map, non-motion event set.
3. Iterate all rules; for each rule call `_processSingleRule`.
4. Feed back emitted messages as variables (`setVariablesFromOwnMessages`).
5. Clear non-motion events (`clearNonMotionEvents`).
6. Return `{messages, usedVariables}`.

---

### 2. Event history

Two types of events are maintained:

**Motion events** (`_motionEventList`):
- Added only when `value !== 0`.
- Stored as `{topic, timestamp, id}` sorted by insertion order.
- Capped at 100 entries; trimmed by 20 when exceeded.
- Retrieved as "latest related" set: the most recent event plus all events within 5 seconds of it.
- If the latest motion is older than 60 seconds and no inactivity gate is configured → motion map is treated as empty for rule evaluation.

**Non-motion events** (`_nonMotionEventList`):
- One-shot, topic → `true` map.
- Added for any message that is NOT a motion topic.
- Cleared after every `processTasks` cycle.

Classification of a message as motion: topic matches any entry in `config.motionTopics`.

---

### 3. Single-rule processing pipeline (`_processSingleRule` → `ProcessRule.check`)

For each rule:

#### 3.1 Inactivity gate (`durationWithoutMovementInMinutes`)

- If **not set**: pass always. Active motion is the "related recent" set (or empty if stale).
- If **set**: pass only when elapsed time since last motion >= `durationWithoutMovementInMinutes` minutes.
  In that case, motion events are passed as-is (not subject to the 60-second staleness check).

#### 3.2 Active gate (`active: false`)

- If `active === false`: skip rule entirely. No messages produced.

#### 3.3 Time / weekday gate

- If `time` is set: compute start time (evaluated expression → `HH:MM[:SS]`).
  Duration defaults to `"6:00"` (6 hours) if not set.
  Rule passes if `now ∈ [startTime, startTime + duration]` or wrapped over midnight.
- If `weekdays` is set: current weekday must be in list. Weekdays are `"sun"`/`"mon"`/…/`"sat"` (case-insensitive).
- Time gate and weekday gate are both required (AND).
- If `time` is not set: time gate passes always.

#### 3.4 Decision gate (`check`)

- Evaluated as JSON-expression formula.
- If `check` is not set: passes always (default `true`).

#### 3.5 Event gate (`allOf` / `anyOf` / `noneOf` / `allow`)

Only evaluated when `motionEvents` is defined AND at least one of `allOf` / `anyOf` is set.

Combined events: `allEvents = { ...motionEvents, ...nonMotionEvents }`.

Logic:

```
result = _isAllIncluded(rule.allOf, allEvents)   // check if ALL allOf topics are in allEvents
if (!result) {
    result = _isAnyIncluded(rule.anyOf, allEvents) // OR: check if ANY anyOf topic is in allEvents
}
if (result && _isAnyIncluded(rule.noneOf, allEvents)) {
    result = false  // fail if any noneOf topic is present
}
if (result && Array.isArray(rule.allow)) {
    // Check that ALL motion events (not nonMotion) are a subset of (allow + allOf + anyOf)
    // In other words: no unexpected motion topics may be active
    allowedTopics = [...rule.allow, ...rule.allOf, ...rule.anyOf]
    if (!_isSubset(allowedTopics, motionEvents)) {
        result = false
    }
}
```

**Key semantics:**
- `allOf` and `anyOf` are **alternatives** (OR): satisfying either one is sufficient.
- `noneOf` is a veto: any matching event blocks the rule.
- `allow` only applies when it is an **Array** (string `allow` is silently ignored).
- `allow` subset check is only against **motionEvents**, not nonMotionEvents.

#### 3.6 Message generation (`_getMessagsFromRule`)

If all gates pass, messages are created from the rule's `topic` field:

| Topic shape | Behavior |
|-------------|----------|
| `string`    | One message with `rule.value` evaluated as expression |
| `array`     | One message per topic entry, all using `rule.value` |
| `object`    | One message per key; value is the corresponding property value |

QoS defaults to 1 if not set in rule.

#### 3.7 Delivery filter (`extractMessagesToSend` + `update`)

After generating candidate messages, each message is checked individually:

**Redundancy check** (`_isMessageRedundant`):

Requires: history entry for `[ruleName][topic]` exists AND `lastSent` is set.

| Cooldown configured | Cooldown active | Outcome |
|---------------------|-----------------|---------|
| No (`cooldownInSeconds` undefined) | — | Redundant if same value AND rule is NOT event-triggered (`anyOf`/`allOf` absent) |
| Yes | Yes (within cooldown window) | Redundant if same value OR if value is a Date type |
| Yes | No (cooldown expired) | NOT redundant — always emit |

**Delay check** (`_isMessageTooEarly`):

Only applies when `delayInSeconds` is set.

- `tooEarly = true` by default.
- Look up history entry. If entry exists AND value matches: check if `firstFound + delayInSeconds > now`.
- If value differs from history (value changed): entry is not found → `tooEarly = true` until new value stabilizes.

A message is emitted only when **not redundant AND not too early**.

#### 3.8 History update

After filtering:

1. **Update for all derived messages** (`_updateHistoryForAllMessages`):
   - For each derived message (whether or not it will be sent):
     - If value changed: set `firstFound = now`, clear `lastSent`.
     - If value unchanged: do nothing (keep original `firstFound`).

2. **Update lastSent for emitted messages** (`_updateHistoryForMessagesToSend`):
   - For each message that will be sent: set `lastSent = now`.

3. **Clear history if rule produced no candidates** (`_clearHistoryIfNoMessagesAvailable`):
   - Applies only when `cooldownInSeconds` is **not** set AND candidate message list is empty.
   - Sets each rule topic history entry to `{value: null, firstFound: now, lastSent: now}`.
   - Effect: next trigger of the same value is treated as a new event (no dedup suppression).
   - When `cooldownInSeconds` IS set and no candidates: history is preserved (cooldown tracking survives gate inactivity).

---

## Part 2 – C++ Implementation Comparison

For each flow step, the C++ behavior (in `rule_runtime_engine.cpp` and `applyDeliveryControls`) is assessed.

---

### Step 3.1 – Inactivity gate

**C++:** `evaluateEventGates` → reads `durationWithoutMovementInMinutes`. If set: compute elapsed minutes since last motion, return false if not enough elapsed. Then `collectRecentEventTopics` uses the same 60-second staleness threshold when no inactivity gate is configured.

**Verdict: IDENTICAL** ✓

---

### Step 3.2 – Active gate

**C++:** `readActiveFlag` returns false only for `active: false` (bool `false` in node). Skip and clear delivery state.

**Verdict: IDENTICAL** ✓

---

### Step 3.3 – Time / weekday gate

**C++:** `evaluateTimeWindowGate` and `evaluateWeekdayGate` implement the same logic.
- Duration default: 6 hours (`k_default_duration_seconds`). ✓
- Midnight wrap (previous-day start interval). ✓
- Weekday comparison is case-insensitive for `sun..sat`, matching JS behavior.

**Verdict: IDENTICAL** ✓

---

### Step 3.4 – Decision gate

**C++:** Delegated to `SingleRuleProcessor::process` which evaluates the `check` expression.

**Verdict: IDENTICAL** ✓ (assuming expression evaluator parity)

---

### Step 3.5 – Event gate

#### allOf / anyOf logic

**JS:** `allOf` is checked first. Only if `allOf` fails is `anyOf` checked. Result: `(allOf satisfied) OR (anyOf satisfied)`.

**C++:** Uses the same semantics as JS: `(allOf satisfied) OR (anyOf satisfied)`.

**Verdict: IDENTICAL** ✓

---

#### noneOf

**JS:** any matching event in `allEvents` (motionEvents + nonMotionEvents) causes gate to fail.
**C++:** any matching event in `recentEventTopics` (combined set) causes gate to fail.

**Verdict: IDENTICAL** ✓

---

#### allow gate

**JS:**
- Only applied when `rule.allow` is an **Array** (string ignored).
- Builds expanded topic set: `allow + allOf + anyOf`.
- Checks if ALL entries in **motionEvents** (not nonMotionEvents) are a subset of that expanded set.
- Purpose: no unexpected motion topics may be active.

**C++:** Implements the same semantics as JS.

**Verdict: IDENTICAL** ✓

---

### Step 3.6 – Message generation

**C++:** `SingleRuleProcessor::process` handles string / array / object topic shapes and evaluates values.

**Verdict: IDENTICAL** ✓ (assuming parity in SingleRuleProcessor)

---

### Step 3.7 – Delivery filter

#### Redundancy / dedup

**JS behavior when NO cooldown configured:**
- Rules with `anyOf` or `allOf`: message is **never** treated as redundant, even for same value.
- Other rules: redundant if same value.

**C++:** Matches JS behavior; event-triggered rules (`anyOf`/`allOf` array present) are not deduped when no cooldown is configured.

**Verdict: IDENTICAL** ✓

---

#### Cooldown behavior

**JS:** When cooldown is configured and NOT in cooldown → emit always (ignore same-value dedup).
**C++:** When cooldown has expired → emit. ✓

**JS:** When cooldown configured but gate fails (no candidates) → **keep history** (cooldown state survives).
**C++:** Preserves cooldown delivery state across gate misses, matching JS.

**Verdict: IDENTICAL** ✓

---

#### Delay (delayInSeconds)

**JS:** History tracks `firstFound` per value across all derived candidates. Delay is computed from `firstFound`. Value change resets `firstFound`.
**C++:** `candidateSince` tracks when current `candidateHash` first appeared. Reset on hash change.

**Verdict: IDENTICAL** ✓

---

### Step 3.8 – History update / state reset on gate miss

**JS:** Gate miss → clear entries to `{value: null}` only when **no cooldown configured**.
**C++:** Gate miss clears delivery state only for rules without cooldown, and retains cooldown-related state when cooldown is configured.

**Verdict: FUNCTIONALLY EQUIVALENT** ✓

---

### Additional: variable feedback

**JS:** After `processTasks`, emitted messages are fed back as variables via `setVariablesFromOwnMessages`. Rule output topics become immediately available as input variables for the next evaluation cycle.
**C++:** Requires verification that `automation_client_component` feeds emitted message topics back into the variable map before the next evaluation.

**Note N1 – Variable feedback needs verification in C++.**

---

## Part 3 – Summary of Deviations

| ID | Component | JS Behavior | C++ Behavior | Behavior Change? |
|----|-----------|-------------|--------------|-----------------|
| N1 | Variable feedback | Emitted topics available as variables in next cycle | Needs verification | Uncertain |

---

## Part 4 – Implemented Fixes

Implemented in `src/yaha/automation_client/rule_runtime_engine.cpp`:
- Case-insensitive weekday parsing (`sun..sat`) for parity with JS.
- Event gate semantics aligned to JS: `allOf`/`anyOf` combination uses OR logic with the same missing-vs-empty handling.
- `allow` semantics aligned to JS subset behavior and applied only when `allow` is an array.
- Event-triggered dedup behavior aligned to JS (no dedup when no cooldown).
- Cooldown delivery-state retention across gate misses aligned to JS.
