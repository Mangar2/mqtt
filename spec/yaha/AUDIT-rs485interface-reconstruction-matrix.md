# RS485Interface Reconstruction Matrix Audit

Date: 2026-05-17

Scope:
- Verify whether spec/yaha/SPEC-rs485interface.md is sufficient to reconstruct legacy behavior from spec/@mangar2/rs485interface/*.js with functionally identical communication behavior.

Method:
- Module-by-module mapping of legacy behavior blocks to SPEC sections.
- Status per module: FULL, PARTIAL, MISSING.
- Any PARTIAL/MISSING item is considered blocking for 100% reconstruction until fixed.

## Module Coverage Matrix

| Legacy Module | Main Behavior Blocks | SPEC Mapping | Status |
|---|---|---|---|
| rs485interface.js | startup/shutdown, serial open/send retries, mqtt bridge, trace topic, time-of-day loop | 2, 4.2, 11, 12, 14, 15 | FULL |
| rs485schedule.js | tick order, maySend gating, queue send/retry, response dequeue, version override on send | 10.2, 10.3, 10.4, 10.5, 14 | FULL |
| rs485tokenexchange.js | token processing, sibling updates, state signaling, version adaptation gate | 9.1, 9.2, 9.3, 9.4 | FULL |
| rs485state.js | constructor defaults, transitions, timers, tokenLost, setState side effects | 8.1, 8.2, 8.3, 8.4 | FULL |
| serialmessage.js | decode v0/v1, encode v0/v1, flags/length, crc/parity, response predicate, logging/introspection | 5.1, 5.2, 5.3, 5.4, 5.5, 12.1, 10.4 | FULL |
| readmessages.js | noise skip, extraction loop, parse failure advance quirk | 6 | FULL |
| sendqueue.js | replacement semantics, command X exception, head dequeue semantics | 10.1 | FULL |
| serialdns.js | mqtt->serial map, serial->mqtt map, explicit topics, interfaces, switch bits | 13 | FULL |
| actions.js | /set, /temporary, /blink suffix checks and timing behavior | 11 | FULL |
| configuration.js | schema constraints, defaults, enums, ranges, trace mismatch context | 3.1, 3.2 | FULL |
| derivesubscribes.js | wildcard derivation, settings subscriptions, explicit topics, control namespace | 4.1, 4.2 | FULL |
| crc16.js | CRC16 algorithm constants and bitflow, parity XOR | 5.3 | FULL |
| addresschain.js | ordered insert, non-functional impact on protocol decisions | 9.2, 9.4 note | FULL |

## Critical Quirks Covered

- Trace singular/plural mismatch behavior (error vs errors).
- Blink suffix check uses endsWith('blink') without leading slash.
- leftmostSibling null coercion behavior in min() and broadcast side effect.
- Retry counter reset only on dequeue path in queue send logic.
- Response dequeue does not reset retry counter.
- First ENABLE_SEND uses maxVersion before negotiation becomes active.
- Scheduler overwrites outgoing message.version with negotiated version.

## Reconstruction Verdict

Current verdict: YES.

Reason:
- All previously identified PARTIAL/MISSING blocks are now covered in SPEC, including explicit encode behavior and introspection/response predicate details.
- No remaining blocking gap was found for communication behavior, timing, queue/retry semantics, token logic, or error-path compatibility.

## Residual Risk Notes

- Legacy JavaScript runtime quirks (coercion and async ordering) are documented and must be preserved by implementation and tests.
- 100% identity still depends on strict parity tests during implementation, especially long deterministic replay sequences.
