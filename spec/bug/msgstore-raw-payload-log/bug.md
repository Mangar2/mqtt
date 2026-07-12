## user report

der msgstore loggt zum reason noch "raw" information. Das hatte ich mal zum debuggen
eingebaut ,bitte wieder ausbauen

## clarification

Asked user whether to remove the `raw=...` field from the shared `message_log_formatter.cpp`
entirely (all clients) or only suppress it for msgstore. User clarified the actual intent:
remove the code in `broker_transport.cpp` that *sets* `Message.rawPayload()` for regular
(non-envelope) incoming messages. Keep the formatter's ability to print `raw=...` whenever a
message legitimately carries a raw payload (e.g. relay/envelope forwarding, zwave, HTTP MQTT
interface publish-forward — all untouched).

## confirmed facts

- `src/yaha/mqtt_client/broker_transport.cpp`, `parsePublishPacketLocked`: the fallback branch
  for ordinary (non-envelope) incoming PUBLISH packets called
  `incomingMessage.setRawPayload(payloadText)` unconditionally, attaching the raw MQTT wire
  payload text to every plain incoming `Message` for every YAHA client using this transport.
  This was the source of the `raw=...` suffix the user saw after `reason=[...]` in msgstore's
  incoming-message log.
- The *other* place `rawPayload` is set — inside `parseEnvelopePayload`
  (`src/yaha/message/message_payload_codec.cpp`), used for JSON-envelope-format forwarded
  messages (relay/broker_connector lossless forwarding) — is a separate, deliberate mechanism
  documented in `src/yaha/mqtt_client/SPEC.md:91` and untouched by this change.
- `message_log_formatter.cpp`'s `raw=...` output capability is untouched; it still prints
  `raw=...` for any message that carries a raw payload through the envelope path.

## resolution

Fix: removed `incomingMessage.setRawPayload(payloadText);` from the plain-message branch of
`parsePublishPacketLocked` in `src/yaha/mqtt_client/broker_transport.cpp`. Ordinary incoming
messages (the vast majority — most MQTT clients don't publish JSON envelopes) no longer carry
a raw payload, so `raw=...` no longer appears in msgstore's (or any other client's) regular
message-flow log. Envelope-forwarded messages (relay, msgstore-consumed relay envelopes) keep
`rawPayload` and `raw=...` exactly as before, since that path is untouched.

Test update: `src/yaha/mqtt_client/test/broker_transport_test.cpp`
(`broker_transport_connect_poll_publish_and_unsubscribe_roundtrip`):
- `received_messages[0]` (plain numeric message): added `CHECK_FALSE(...rawPayload().has_value())`
  as a regression guard for the removed behavior.
- `received_messages[7]` (an envelope with an invalid `value` field, so `parseEnvelopePayload`
  rejects it and it falls through to plain decoding): updated the previously-passing
  `REQUIRE(...rawPayload().has_value())` / `CHECK(*...rawPayload() == ...)` pair (which
  asserted the old, now-removed side effect) to `CHECK_FALSE(...rawPayload().has_value())`.
  The decoded `.value()` for this case already equals the full raw text independently, so no
  information is lost.

Files touched:
- `src/yaha/mqtt_client/broker_transport.cpp` (fix)
- `src/yaha/mqtt_client/test/broker_transport_test.cpp` (test updates)

Verification: `python3 test/run_coverage_clients.py` — full client suite green after the fix
(failed once with the expected assertion before the test update, confirming the mechanism),
coverage threshold met, no other file regressed.
