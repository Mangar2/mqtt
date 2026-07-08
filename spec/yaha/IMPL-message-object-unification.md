# YAHA Client Message Object Unification Plan

## Goal

Every YAHA client consumes incoming MQTT messages and produces outgoing MQTT messages exclusively
as `yaha::message::Message` (`src/yaha/message/message.h`) — including internally, e.g. for logging.
No client keeps a parallel/replacement object that duplicates what `Message` already carries
(topic, value, reason, qos, retain, dup).

Primary reference behavior:
- [spec/@mangar2/mqtt-utils/src/message.ts](../@mangar2/mqtt-utils/src/message.ts)

Primary specification to enforce:
- [SPEC-message.md](SPEC-message.md)

Related, earlier plans this one continues:
- [spec/message-format-unification-plan.md](../message-format-unification-plan.md) — unified JSON
  envelope parsing/building (topic/value/reason). Marked "Completed" for its own scope
  (automation_client, remote_service, remote_service_http, value_service, transport layer).
  Did **not** cover qos/retain/dup duplication, manual "copy Message with overrides" code, or a
  concrete envelope-bypass bug found in `broker_connector` (see below).
- [IMPL-message-logging-unification.md](IMPL-message-logging-unification.md) — unified message-flow
  logging via `buildMessageLogLine`/`message_log_service`. Marked "Completed" for Phase 3 (incl.
  `rs485_interface`) and Phase 4 (incl. `broker_connector` "source incoming + receiver outgoing
  paths"). **Verified against current code: this status is only partially accurate** — see
  "Status correction" below. This plan finishes that gap and extends scope to clients that were
  never covered by it (`file_store`, `pushover`, `remote_service`, `opensensemap`).

## Status correction for IMPL-message-logging-unification.md

Grepped `buildMessageLogLine`/`logIncomingMessage*`/`logOutgoingMessage*`/`message_log_service`
usage per module against the "Completed" claims:

| Module | Claimed status | Actual code state |
|---|---|---|
| `rs485_interface` | Phase 3, Completed 2026-05-19 | **No usage found.** Only RS485-wire-level logging (`buildLegacyLoggingInfo`) exists; no MQTT-`Message`-level incoming/outgoing log. |
| `broker_connector` source side (`source_http_adapter.cpp`) | Phase 4, Completed 2026-05-19 | Confirmed correct — uses `buildMessageLogLine` (`source_http_adapter.cpp:481-496`). |
| `broker_connector` receiver/relay side (`relay_component.cpp`, `receiver_publish_port.cpp`) | Phase 4, Completed 2026-05-19 ("receiver outgoing paths") | **No usage found.** `relay_component.cpp` only tracks numeric `RelayCounters`, no message-flow log line at all. |
| `message_store_client` | Phase 4, Completed 2026-05-19 | Confirmed correct — `message_store_client_app.cpp` uses it. |
| `http_mqtt_interface_client` | Phase 4, Completed 2026-05-19 | Confirmed correct — `http_mqtt_interface_client_app_helpers.cpp` uses it. |

This plan treats `rs485_interface` and `broker_connector` (relay/receiver side) as **not done** and
finishes them, rather than assuming the earlier "Completed" marker is reliable. Once finished, that
status table in `IMPL-message-logging-unification.md` should be corrected too (see Phase E below).

## Scope

### Confirmed already Message-based (out of scope)

- Full transport layer: `IMqttComponent::handleMessage`, `YahaMqttClient`, `BrokerTransportAdapter`,
  `message_payload_codec` — every client already receives/sends `Message` objects at the framework
  boundary.
- `message_store` MQTT ingestion path: `MessageStore::handleMessage(const Message&)` →
  `MessageTree::addData(message)` — 100% `Message`-based already (see "MessageSnapshot" section below
  for why this needed explicit verification).
- `opensensemap`, `pushover`, `value_service`, `automation_client`, `zwave`, `serial_device`,
  `rs485_interface` outgoing path — all publish via `Message` objects already.

### Decisions locked in with the project owner (do not revisit without discussion)

1. **`qos_`/`retain_`/`dup_` stay concrete types, not `std::optional`.** The JS original
   (`message.ts`) also defaults them concretely on the `Message` class itself (`qos=1`,
   `retain=false`); `dup` isn't even a class field there — it's always a separate parameter.
   `undefined` in the original only exists on the `IMessage` *interface* for raw object literals
   before construction, not as `Message` class behavior. No optionality change in this plan.
2. **`MessageSnapshot` (`message_store/message_tree.h:24-29`) stays as-is.** It is a
   purpose-built HTTP diff-query DTO (client sends last-known state, store returns what changed),
   not a message-transport type. Verified: `MessageTree::addData` (the tree write/ingestion path)
   never takes a `MessageSnapshot`; `MessageSnapshot` only flows through `queryNodes`/`getNodes`/
   `snapshotEquals` (read/compare paths) and the HTTP snapshot-diff request body parser
   (`parseSnapshotBody`). This plan adds a guard/test to keep it that way, not a rewrite.
3. **`packetId` stays outside `Message`.** It is MQTT ack/handshake bookkeeping (QoS1/2 PUBACK/
   PUBREC correlation), not message content like topic/value/qos/retain/dup.

### Confirmed replacement objects / bypasses to eliminate

| # | Location | Problem |
|---|---|---|
| A | `broker_connector/source_http_adapter.h:47-52` `SourcePublishMeta{qos, retain, dup, packetId}` | Duplicates `Message::qos()/retain()/dup()`; threaded alongside `Message` via `SourcePublishCallback = std::function<void(const Message&, const SourcePublishMeta&)>`. |
| B | `broker_connector/receiver_publish_port.h:22-26` `ReceiverPublishOptions{qos, retain, dup}` | Same duplication on the publish side (`ReceiverPublishPort::publish(const Message&, const ReceiverPublishOptions&, ...)`). |
| C | `http_mqtt_interface/http_mqtt_interface_dispatcher.h:57-62` `HttpMqttPublishOptions.dup` | Duplicates `Message::dup()` even though the struct already embeds `Message message`. |
| D | `broker_connector/relay_component.cpp:17-143,262-301` | `toForwardMessage` builds a new `Message` **field by field** instead of via `Message::clone()` + setters. For `rawPayload` topic rewriting it uses hand-rolled JSON substring surgery (`tryFindObjectRange`, `tryFindStringValueRange`, `tryRewriteForwardedEnvelopeTopic`, a local `escapeJsonString`) instead of `message_payload_codec::buildEnvelopePayload`. **The reason-copy loop (lines 296-298) has a real ordering bug**: `message.reason()` is newest-first internally; the loop calls `mapped.addReason(...)` in that order, and `addReason` prepends — so `mapped`'s reason chain ends up reversed (oldest-first). `zwave_service_component.cpp:48-57` avoids the bug by reversing with `std::views::reverse` before replaying, but that's still a local reimplementation, not a shared fix. |
| E | `rs485_interface/rs485_interface_component.cpp:208-209`, `serial_device/serial_device_component.cpp:110-113`, `zwave/zwave_service_component.cpp:48-57,276-280` | Each component reimplements its own "copy Message with changed fields" (some correct, some buggy as in D) instead of `clone()` + setters. `zwave_service_component.cpp:48-57` additionally has a local free function `withPublishFlags(...)` that duplicates what the new `Message` setters will do — remove it in Phase C/E and call the setters directly. |
| F | Logging gaps: `file_store.cpp:87-98` (bespoke `logMessage`, prints only the first reason entry), `pushover_component.cpp:25,32` (`logError`/`logHttpError` take only `topic: std::string`, no `Message`), `rs485_interface_component.cpp` (no `Message`-level log at all — see status correction above), `remote_service_component.cpp` (no `Message`-level log), `broker_connector/relay_component.cpp` (no `Message`-level log — see status correction above), `opensensemap_component.cpp:32-46` (manual formatting instead of `buildMessageLogLine`) | Violates `SPEC-message.md`'s "Canonical Message-Flow Logging Contract" (shared formatter, deterministic field order incl. `qos`/`retain`/`dup`, full reason chain). |

## Target Architecture

### Extend `Message`'s existing clone-and-mutate pattern

`Message` already has this pattern for one field: `setDup(bool)` mutates in place, and callers combine
it with `clone()` when they need an independent copy. Extend the same pattern to the two other fields
components currently need to change — no new method names, no new struct:

Add to `src/yaha/message/message.h` / `message.cpp` (additive, no existing signature changes):

```cpp
void setTopic(std::string topic);
void setQos(Qos qos) noexcept;
void setRetain(bool retain) noexcept;
```

Call sites replace field-by-field reconstruction with `clone()` + targeted setters, e.g.:

```cpp
Message mapped = message.clone();
mapped.setTopic(targetTopic);
mapped.setQos(targetQos);
mapped.setRetain(targetRetain);
mapped.setDup(targetDup);
```

`clone()` already copies the reason chain and `rawPayload` correctly (it's a plain value copy), so this
also removes the reason-order bug in finding D for free — there is no manual reason-copy loop left to
get wrong. A dedicated "with"-style builder (returning a new `Message` for one hardcoded combination of
fields) was considered and rejected: it would force picking arbitrary field groupings up front, and adds
a second construction style next to `clone()`/`setDup()` instead of reusing it.

## Migration Phases

### Phase A: Message API extension

- Add `setTopic(std::string)`, `setQos(Qos)`, `setRetain(bool)` to `message.h`/`message.cpp`, mirroring
  the existing `setDup(bool)` mutator.
- Unit tests in `src/yaha/message/test/message_test.cpp`: each setter mutates only its own field;
  `clone()` followed by any combination of setters leaves the reason chain order and `rawPayload`
  untouched (this is the case that matters for finding D — no new reason-copy logic is introduced, so
  the existing `clone()` coverage mostly already proves this, add one combined test for documentation).
- Purely additive — no existing behavior changes.

Status: done.

### Phase B: Remove parallel qos/retain/dup carriers (findings A, B, C)

- `SourcePublishMeta`: drop `qos`/`retain`/`dup`, keep only `packetId`. `SourcePublishCallback`
  delivers a `Message` already populated via `clone()` + `setQos`/`setRetain`/`setDup`; callers read
  qos/retain/dup from `message.qos()/retain()/dup()`.
- `ReceiverPublishOptions`: drop `qos`/`retain`/`dup`; `ReceiverPublishPort::publish` takes only
  `const Message&`. `ReceiverMqttPublishPort::applyPublishOptions` is removed or becomes a pure
  pass-through.
- `HttpMqttPublishOptions.dup` removed; callers call `Message::setDup` on `options.message` before
  populating `HttpMqttPublishOptions`.
- Update tests: `broker_connector/test/source_http_adapter_test.cpp`,
  `broker_connector/test/relay_component_test.cpp`, receiver-publish-port tests, `http_mqtt_interface`
  tests referencing `HttpMqttPublishOptions.dup`.

Status: not started.

### Phase C: Remove `relay_component.cpp` envelope bypass (finding D)

- Delete `tryFindObjectRange`, `tryFindStringValueRange`, `tryRewriteForwardedEnvelopeTopic`, the
  local `escapeJsonString`.
- `toForwardMessage` builds the target message via `message.clone()` plus `setTopic`/`setQos`/
  `setRetain`/`setDup` (Phase A) instead of the field-by-field constructor call and the manual
  reason-copy loop — this also fixes the reason-order bug, since `clone()` copies the reason chain
  correctly and no replay loop remains.
- When `rawPayload` exists and the topic changes, re-serialize via
  `message_payload_codec::buildEnvelopePayload(mapped)` instead of substring surgery; unchanged-topic
  case keeps passing `rawPayload` through unmodified (lossless forwarding), as today.
- Update `relay_component_test.cpp`: cover multi-entry reason chains through a topic rewrite (this is
  the case the current bug silently breaks) and confirm no substring-based rewriting remains.

Status: not started.

### Phase D: Guard `MessageSnapshot` boundary (decision 2 above)

- No functional change to `MessageSnapshot`, `message_tree.*`, `message_store_json_parser.*`.
- Add a short comment on `MessageSnapshot` (`message_tree.h:24-29`) stating it must only be used for
  HTTP diff-query bodies and must never represent an incoming MQTT message.
- Add a regression test (in `message_store/test/message_tree_test.cpp` or `message_store_test.cpp`)
  that pins down `MessageTree::addData` as the only tree-write entrypoint and asserts it is
  `Message`-typed, so a future change can't silently reintroduce `MessageSnapshot` into the write path.

Status: not started.

### Phase E: Logging unification (finding F, and closing the gap from IMPL-message-logging-unification.md)

Migrate the following onto `buildMessageLogLine`/`message_log_service` (pattern already proven in
`mqtt_client.cpp`, `value_service_component.cpp`, `zwave_service_component.cpp`,
`source_http_adapter.cpp`), for both incoming and outgoing paths:

- `rs485_interface_component.cpp` — add `logIncomingMessageIfEnabled`/`logOutgoingMessageIfEnabled`
  (currently fully missing, despite prior "Completed" status). Keep `buildLegacyLoggingInfo` as-is —
  that's RS485-wire-level logging, a different concern.
- `broker_connector/relay_component.cpp` — add message-flow logging for `onIncomingPublish`/
  `toForwardMessage` (currently only numeric `RelayCounters`, despite prior "Completed" status).
- `file_store.cpp` — remove bespoke `logMessage` (prints only the first reason entry), use
  `buildMessageLogLine` (call site `file_store.cpp:152`).
- `pushover_component.cpp` — change `logError`/`logHttpError` from `(reasonText, topic: std::string)`
  to `(reasonText, const Message&)` (call sites `pushover_component.cpp:112,122,148,154,162`).
- `opensensemap_component.cpp` — replace manual formatting (lines 32-46) with `buildMessageLogLine`
  (signature already takes `const Message&`).
- `remote_service_component.cpp` — add message-flow logging (currently none at all).
- Update/add per-component log-format tests so the deterministic field order from `SPEC-message.md`
  (`component, direction, topic, value, qos, retain, dup, reason`) holds everywhere.
- Once done, correct the status table in `IMPL-message-logging-unification.md` for `rs485_interface`
  and `broker_connector` receiver/relay side.

Status: not started.

### Phase F: Documentation sync

- `SPEC-message.md`: the Fields table lists `topic`, `value`, `reason`, `qos`, `retain` but not
  `dup`, even though `message.h` has it and the logging contract further down requires it — add the
  missing row.
- `src/yaha/message/SPEC.md`: "no external dependencies" note omits `<optional>`, which `message.h`
  already includes (for `rawPayload`) — correct in passing if touched anyway.
- Cross-link this document from `spec/message-format-unification-plan.md` and
  `IMPL-message-logging-unification.md` so the plan history stays discoverable.

Status: not started.

## Explicitly out of scope

- `qos_`/`retain_`/`dup_` as `std::optional` (decision 1).
- Rewriting `MessageSnapshot` itself (decision 2).
- Adding `packetId` to `Message` (decision 3).
- `SerialDeviceMessage` / `Rs485SerialMessage` — legitimate non-MQTT wire-protocol adapters, not
  `Message` duplicates.
- Anything `spec/message-format-unification-plan.md` already marked "Completed" (JSON envelope
  parsing/building in `automation_client`, `remote_service*`, `value_service`).

## Acceptance Criteria

- No component under `src/yaha` owns a struct that duplicates `Message`'s qos/retain/dup alongside a
  `Message` parameter.
- No component reconstructs a `Message` field-by-field where `clone()` + `setTopic()`/`setQos()`/
  `setRetain()`/`setDup()` would do.
- `broker_connector` forwards messages exclusively through `Message` + `message_payload_codec`, no
  hand-rolled JSON substring editing.
- Every YAHA client with message-flow logging uses `buildMessageLogLine`/`message_log_service` for
  both incoming and outgoing messages, with the full reason chain and deterministic field order.
- `MessageSnapshot` remains scoped to HTTP diff-query use only, with a regression test enforcing it.

## Validation Strategy

- `yahabroker-tests` (links all YAHA module sources + tests) green after every phase.
- Integration test for Phase B/C:
  `test/yaha/broker_connector_client/forwarding_runtime.py` (end-to-end forwarding incl. topic
  rewrite and reason chain, exercising the bug fixed in Phase C).
- After all phases: grep sweep over `src/yaha` for `Qos qos{`, `bool retain{`, `bool dup{` outside
  `message.h`/`message_log_*`, and for manual `Message name{...}` constructions with more than 3
  positional arguments, to confirm no new replacement objects or copy loops were introduced.
- Follow `/cpp-dev` rules (C++20, max 1000 lines/file, English-only comments) for every changed/new
  file, then run the `/unit-test` and `/integration-test` skill workflows, per `CLAUDE.md`.

## Risks and Mitigations

- Risk: removing `SourcePublishMeta`/`ReceiverPublishOptions` qos/retain/dup fields breaks call sites
  that read them directly instead of from `Message`.
  - Mitigation: compiler-enforced (removed fields fail to compile at every call site); fix forward
    with the same phase, no silent runtime gap.
- Risk: `relay_component.cpp` rewrite changes forwarded envelope byte-for-byte shape for unaffected
  (same-topic) messages.
  - Mitigation: same-topic path keeps passing `rawPayload` through unchanged, only the topic-changed
    path switches from substring surgery to re-serialization; add a byte-identical regression test for
    the unchanged-topic case.
- Risk: log format changes break external log parsers relying on the old per-component formats
  (`file_store`, `pushover`).
  - Mitigation: these old formats were never part of the documented contract (`SPEC-message.md`);
    document the change and, if needed, coordinate with any known log consumers before merging Phase E.
