## user report

es muss bereits einen ersten "fix" geben aber der war wohl nicht durchschlagend. Der git checkin "Remove raw payload ..." sollte verhindern, dass "raw" felder an meldungen gehängt werden. ich sehe aber als beispiel im automation client weiterhin im logging ein "raw" feld mit Inhalt. Meine Inforation war, das wird geloggt, weil es in broker_transport automatisch an die message angehängt wird und meine bitte war zu verhindern, dass es angeängt wird mit der erwartungshaltung, dass wir dann im log auch kein "raw" mehr sehen. Ich will explizit, dass die fähigkeit das raw feld zu loggen (wenn es vorhanden ist) bestehend bleibt. Damit kann ich dann auch kontrollieren, dass wirklich kein raw feld mehr angehängt wird.

## related prior work

- `spec/bug/msgstore-raw-payload-log/bug.md`: prior bug, fix commit `85c8c43` "Remove raw payload
  assignment for regular incoming messages to prevent unnecessary logging". That fix removed
  `incomingMessage.setRawPayload(payloadText)` from the plain-message fallback branch in
  `src/yaha/mqtt_client/broker_transport.cpp`'s `parsePublishPacketLocked`. Confirmed still absent
  in current source (grep finds no `setRawPayload` call in that file).
- The `message_log_formatter.cpp` capability to print `raw=...` when a message carries a raw
  payload must stay untouched/working (explicit user requirement, repeated here).

## preliminary finding (step 4 candidate, not yet confirmed root cause)

`src/yaha/automation_client/automation_client_component.cpp:512` inside the rule-trace
publishing function calls:

```
traceMessage.setRawPayload(automation_trace_format::buildTraceRawPayload(traceMessage));
```

This is a separate call site from the one fixed in `broker_transport.cpp` — it explicitly builds
and attaches a raw payload to a debug-trace `Message` published to a topic built from
`k_debug_topic_prefix` / `buildTraceTopicFromRuleLink`. This looks like a deliberate,
purpose-built trace/debug mechanism (`automation_trace_format::buildTraceRawPayload`), not a
leftover of the bug fixed in `broker_transport.cpp`.

## user-provided log evidence

```
Jul 12 18:59:50 yahapi yahaautomationclient[24566]: automation_client <- first/study/main/motion sensor/detection state : 1.000000 qos=1
Jul 12 18:59:50 yahapi yahaautomationclient[24566]: reason=[{"message":"received from arduino","timestamp":"2026-07-12T16:59:50Z"}] raw="{\"message\":{\"topic\":\"first/study/main/motion sensor/detection state\",\"value\":1.000000,\"reason\":[{\"timestamp\":\"2026-07-12T16:59:50Z\",\"message\":\"received from arduino\"}]}}"
```

This is a regular inbound sensor message (topic is a normal domain topic, not the
`k_debug_topic_prefix` debug-trace topic), so the preliminary automation-trace-mechanism finding
above is ruled out as the cause of this particular log line.

## root cause (confirmed by code reading, step 4)

`src/yaha/mqtt_client/broker_transport.cpp:472-480` (`parsePublishPacketLocked`) tries
`parseEnvelopePayload` (`src/yaha/message/message_payload_codec.cpp:168`) for every inbound
PUBLISH before falling back to plain decoding. Per `src/yaha/mqtt_client/SPEC.md:85-86`, **every**
outgoing publish from any broker_transport-based YAHA client is serialized as canonical YAHA
envelope JSON (`{"message":{"topic":...,"value":...,"reason":...}}`) — not only
relay/broker_connector forwarding traffic. Consequently `parseEnvelopePayload` succeeds for
essentially all inbound messages from other YAHA clients (as in the log example above), and it
unconditionally calls `setRawPayload(payloadText)` at lines 210, 220, and 235 of
`message_payload_codec.cpp`. This preserves the raw envelope text "for lossless forwarding"
(SPEC.md:91) — a deliberate mechanism for components that actually republish/relay messages
byte-for-byte (`relay_component.cpp`, `broker_connector`/`source_http_adapter.cpp`). But
`automation_client` is a leaf consumer that never re-forwards received messages, so it has no
functional need for `rawPayload`; the attached raw payload serves no purpose there except
appearing in the log.

This explains why the earlier fix (`85c8c43`, scoped only to the non-envelope plain-message
fallback) did not stop `raw=...` from appearing: the vast majority of real inbound traffic
between YAHA clients matches the envelope shape and takes the still-unmodified envelope branch.

## open question for user (scope decision, not yet implemented)

Two fix directions, mutually exclusive:

1. **Per-consumer opt-in**: only components that genuinely need to preserve/re-forward the raw
   envelope text (`relay_component`, `broker_connector`/`source_http_adapter`) keep
   `rawPayload`; other leaf-consuming clients (automation_client, msgstore, zwave, http_mqtt
   interface, etc.) never receive a `rawPayload` on inbound messages, even when the payload is
   envelope-shaped. Requires plumbing a flag/parameter through `broker_transport` construction
   (or a callback) so `parsePublishPacketLocked` knows whether the owning client relays messages.
2. **Keep envelope raw everywhere, but stop it from being emitted so broadly** — e.g., something
   narrower than #1. (No concrete alternative identified yet; user requested option to weigh in
   before scope is locked.)

User's explicit requirement carried into this decision: the formatter's ability to print
`raw=...` when a message legitimately carries a raw payload must remain untouched — this is a
verification tool for the user, not something to remove.

## user confirmation of fix direction

User approved the "per-client opt-in flag, default false" approach, with a terminology
correction: the client that needs the flag is `broker_connector` (specifically its receiver-side
`YahaMqttClient` instance, implemented in `src/yaha/broker_connector/receiver_publish_port.cpp`,
which feeds `relay_component.cpp`'s forwarding logic) — not "relay_component" as a standalone
YAHA client (no such client exists).

## resolution

Root cause: `src/yaha/mqtt_client/broker_transport.cpp`'s `parsePublishPacketLocked` tries
`parseEnvelopePayload` (`src/yaha/message/message_payload_codec.cpp:168`) for every inbound
PUBLISH. Per `SPEC.md:85-86`, every outgoing publish from any broker_transport-based YAHA client
is serialized as canonical YAHA envelope JSON, so `parseEnvelopePayload` succeeds for essentially
all inbound traffic between YAHA clients (not just relay/forward traffic), and it unconditionally
called `setRawPayload(payloadText)`. The earlier fix (`85c8c43`) only removed the narrower
non-envelope plain-message fallback case, leaving this much more common envelope case untouched
— which is why `raw=...` kept appearing (e.g. in automation_client's log for a plain incoming
sensor message from arduino, matched by the user's log excerpt above).

Fix: added `YahaMqttClient::Config::preserveRawEnvelopePayload` (default `false`,
`src/yaha/mqtt_client/mqtt_client.h`). `BrokerTransportAdapter` stores this flag on `connect()`
and, in `parsePublishPacketLocked`, clears `rawPayload()` on envelope-parsed messages
(`Message::clearRawPayload()`) unless the flag is `true`
(`src/yaha/mqtt_client/broker_transport.cpp`). Only `broker_connector`'s receiver client sets
the flag to `true`, in `ReceiverMqttPublishPort::toClientConfig`
(`src/yaha/broker_connector/receiver_publish_port.cpp`), since it is the sole component that
reads `rawPayload()` for lossless byte-for-byte forwarding (`relay_component.cpp:160`). All
other YAHA clients (automation_client, msgstore, zwave, http_mqtt_interface, rs485,
serial_device, ...) now never receive a `rawPayload` on inbound messages, regardless of whether
the wire payload happened to be envelope-shaped.

The `message_log_formatter.cpp` capability to print `raw=...` when a message legitimately
carries a raw payload is untouched (verified: `message_log_formatter.cpp` at 100% coverage,
no changes made to that file).

Files touched:
- `src/yaha/mqtt_client/mqtt_client.h` (new `Config::preserveRawEnvelopePayload` field)
- `src/yaha/mqtt_client/broker_transport.cpp` (fix: conditional `clearRawPayload()`)
- `src/yaha/mqtt_client/SPEC.md` (doc update)
- `src/yaha/broker_connector/receiver_publish_port.cpp` (opt-in for the one genuine consumer)
- `src/yaha/broker_connector/SPEC.md` (doc update)
- `src/yaha/mqtt_client/test/broker_transport_test.cpp` (updated roundtrip-test expectations to
  the new default-off behavior; added dedicated
  `broker_transport_preserves_raw_envelope_payload_when_opted_in` test for the opt-in path)

Verification: `python3 test/run_coverage_clients.py` — 1173/1173 tests green, coverage threshold
met for all touched files (`broker_transport.cpp` 82.09%/100%/80.33% regions/functions/lines,
`receiver_publish_port.cpp` 86.00%/84.62%/86.67%), no other file regressed.

Test location: kept in existing `src/yaha/mqtt_client/test/broker_transport_test.cpp` (standard
unit test suite location for this module; no separate repro script needed since the mechanism is
deterministic and fully covered by the existing fake-broker-based test harness).

Status: CLOSED. User confirmed on the live system: automation_client log no longer shows
`raw=...` for regular inbound messages. Original reported symptom resolved.
