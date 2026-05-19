# message test specification

## Scope

Unit tests for Message value-type behavior and validation guarantees.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `Message construction with string value` | default constructor values with string payload | topic + string payload | topic/value preserved, qos=1, retain=false |
| `Message construction with double value` | explicit qos/retain constructor path | topic + double payload + qos/retain | fields match input |
| `Message isOn string values` | string truth mapping | `on`, `ON`, `true`, other strings | only canonical true values return true |
| `Message isOn double values` | numeric truth mapping | `1.0`, `0.0`, other doubles | only `1.0` returns true |
| `Message addReason with explicit timestamp` | append reason with caller timestamp | one addReason call | reason list contains provided entry |
| `Message addReason prepends, most recent at front` | reason order semantics | two addReason calls | newest reason is index 0 |
| `Message addReason auto-generates timestamp` | generated timestamp path | addReason(text) | non-empty timestamp generated |
| `Message clone is independent copy` | deep-copy behavior | clone + mutate copy reason list | original remains unchanged |
| `Message raw payload can be stored copied and cleared` | raw payload lifecycle for lossless forwarding | set raw payload, clone, clear on clone | payload exists on original+clone, clear affects clone only |
| `Message validate passes for valid message` | positive validation path | valid topic/reasons | no exception |
| `Message validate rejects empty topic` | required topic validation | empty topic | throws invalid_argument |
| `Message validate rejects ReasonEntry with empty message` | reason validation | reason entry with empty message | throws invalid_argument |
| `Message qos AtMostOnce construction` | QoS enum path | qos=AtMostOnce | qos getter returns enum value |
| `Message dup flag can be constructed and updated` | DUP state API | construct with dup=true then setDup(false) | dup getter reflects both states |

## Payload codec test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `Payload codec escapes JSON control characters` | string escaping for shared builder path | text with quotes, slash, newline, tab | escaped JSON content contains escaped tokens |
| `Payload codec serializes reasons oldest first` | reason order on wire | Message with two reasons (newest-first in object) | serialized reason JSON places oldest entry first |
| `Payload codec buildEnvelopePayload emits canonical envelope` | canonical payload build | Message with topic/value/reason | payload contains top-level message object with required keys |
| `Payload codec parseEnvelopePayload parses numeric value` | numeric payload parsing | envelope with value=77.5 | parsed Message value is double 77.5 |
| `Payload codec parseEnvelopePayload coerces bool token to string` | backward-compatible token coercion | envelope with value=true | parsed Message value is string "true" |
| `Payload codec parseEnvelopePayload rejects malformed envelope` | malformed JSON token handling | envelope missing colon in message object | parse returns empty optional |
| `Payload codec parseReasonArray rejects missing message field` | malformed reason object handling | reason array with entry missing message | parse returns empty optional |
| `Payload codec validateEnvelopeShape enforces topic and value` | shape validation | valid payload and payloads missing topic/value | true for valid; false for invalid |
| `Payload codec envelope matches TS reference ordering` | compatibility with TS reference envelope semantics | message with two reasons added in chronological order | built payload contains reasons in same oldest-first order as TS class |
| `Payload codec parse and rebuild keeps TS compatible envelope` | regression guardrail for canonical behavior | TS-style envelope payload with escaped strings and numeric value | parse succeeds and rebuilt payload keeps canonical message shape and values |
| `Payload codec decodes full JSON escape set` | string unescape compatibility guardrail | envelope with escaped quote, slash, backslash, r, b, f, and unicode | parsed value decodes escapes to expected in-memory text |
| `Payload codec rejects invalid escaped token` | malformed escape rejection guardrail | envelope containing unsupported escaped sequence | parse returns empty optional |

## Message logging service test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `Message log filter matches exact topic` | exact-match filter behavior | topic `a/b/c`, filter `a/b/c` | match=true |
| `Message log filter supports plus wildcard` | single-level wildcard match | topic `a/b/c`, filter `a/+/c` | match=true |
| `Message log filter supports hash wildcard tail` | multi-level tail wildcard | topic `a/b/c/d`, filter `a/#` | match=true |
| `Message log filter rejects non-matching topic` | negative filter branch | topic `a/b/c`, filter `a/+/d` | match=false |
| `Message log service disables incoming when flag false` | incoming gate behavior | direction incoming with enableIncoming=false | no log line returned |
| `Message log service disables outgoing when flag false` | outgoing gate behavior | direction outgoing with enableOutgoing=false | no log line returned |
| `Message log service applies incoming topic filter` | direction-aware incoming filter | incoming message with non-matching incoming filter | no log line returned |
| `Message log service applies outgoing topic filter` | direction-aware outgoing filter | outgoing message with non-matching outgoing filter | no log line returned |
| `Message log formatter emits deterministic required field order` | schema ordering contract | message with string value and one reason | log line field order follows required sequence |
| `Message log formatter escapes control characters deterministically` | escape contract | message value/reason text with quote, backslash, newline, tab | escaped output uses JSON-compatible tokens |
| `Message log formatter emits full reason chain` | reason completeness contract | message with multiple reason entries | all reasons rendered in message order |
| `Message log service can omit reason chain by config` | optional reason rendering branch | includeReasonChain=false | reason field omitted or empty per contract |
