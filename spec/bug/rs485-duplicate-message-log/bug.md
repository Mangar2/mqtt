## user report

der rs485 client gibt meldungen doppelt aus hier ein beispiel: Jul 12 16:54:56 yahapi yahars485interfaceclient[12004]:   mqtt: mqtt_client -> first/hallway/main/light/increase light on time in seconds : 10.000000 qos=1
Jul 12 16:54:56 yahapi yahars485interfaceclient[12004]: reason=[{"message":"received from arduino","timestamp":"2026-07-12T14:54:56Z"}]
Jul 12 16:54:56 yahapi yahars485interfaceclient[12004]: rs485_interface -> first/hallway/main/light/increase light on time in seconds : 10.000000 qos=1
Jul 12 16:54:56 yahapi yahars485interfaceclient[12004]: reason=[{"message":"received from arduino","timestamp":"2026-07-12T14:54:56Z"}]

## test case

Existing test suite: `src/yaha/rs485_interface_client/test/rs485_interface_client_config_test.cpp`.

Added test case `rs485_runtime_config_does_not_force_mqtt_client_message_trace` asserting that
enabling `rs485interface.logIncomingMessages`/`logOutgoingMessages` must NOT force on
`mqttConfig.enableMessageTrace` (root-cause condition for the duplicate log lines).

Run command:

```
python3 test/run_coverage_clients.py
```

Result before fix (FAILS, demonstrates bug):

```
[FAILED] Client unit tests
  failing test        : rs485_runtime_config_does_not_force_mqtt_client_message_trace
  exit                : 42

  --- relevant output ---
  src/yaha/rs485_interface_client/test/rs485_interface_client_config_test.cpp:142: FAILED: runtimeConfig.mqttConfig.enableMessageTrace == false for: true == false
  test cases: 1 | 1 failed
  assertions: 4 | 3 passed | 1 failed
```

## scope

Allow:
- `src/yaha/rs485_interface_client/rs485_interface_client_app.cpp` — the `mqttConfig.enableMessageTrace` auto-enable side effect of `rs485interface.logIncomingMessages`/`logOutgoingMessages`.
- `src/yaha/rs485_interface_client/SPEC.md` — doc line describing that auto-enable.
- `src/yaha/rs485_interface_client/test/rs485_interface_client_config_test.cpp` — test assertions covering this config wiring.

Deny:
- `rs485interface.trace` / `buildLegacyLoggingInfo` / RS485 wire-level trace (untouched, out of scope, already MQTT-agnostic).
- `Rs485InterfaceComponent::logIncomingMessageIfEnabled`/`logOutgoingMessageIfEnabled` (component's own MQTT message-flow log) — stays as-is, matches every other client's pattern.
- Any other client (`relay`, `file_store`, `pushover`, `opensensemap`, `remote_service`) or broker-side code.
- `mqtt_client.cpp` itself (generic trace mechanism stays as-is; only the rs485-side forced activation is removed).

## confirmed facts

- Duplicate log lines for one MQTT message ("mqtt_client -> ..." and "rs485_interface -> ...", identical topic/value/qos/reason) are emitted only by `yahars485interfaceclient`.
- Root cause: `rs485_interface_client_app.cpp` sets `parsed.mqttConfig.enableMessageTrace = true` whenever `rs485interface.logIncomingMessages` or `logOutgoingMessages` is enabled. This forces `mqtt_client`'s own generic wire-level message trace on as a side effect, duplicating the message-flow log the `rs485_interface` component already emits itself via the shared `message_log_service` (added in `spec/yaha/IMPL-message-object-unification.md` Phase E).
- No other YAHA client sets `enableMessageTrace` at all (confirmed via `grep -rn "enableMessageTrace" src/yaha/*_client/*.cpp` — zero matches outside `rs485_interface_client`). This auto-enable is unique sonder-code to `rs485_interface_client`.
- User decision: keep RS485 wire trace untouched, keep the component's own MQTT message-flow log untouched (it already matches how every other client logs MQTT messages), delete only the `mqtt_client` auto-enable side effect.

## hypothesis

`src/yaha/rs485_interface_client/rs485_interface_client_app.cpp:490-492` (function `loadRs485InterfaceClientRuntimeConfigFromIni`):

```cpp
if (parsed.rs485Config.logIncomingMessages || parsed.rs485Config.logOutgoingMessages) {
    parsed.mqttConfig.enableMessageTrace = true;
}
```

This is the sole cause of the duplicate. Removing it (leaving `mqttConfig.enableMessageTrace` at its struct default `false`) removes the generic `mqtt_client -> ...`/`mqtt_client <- ...` log lines while the component-level `rs485_interface -> ...`/`rs485_interface <- ...` log lines (already correctly implemented per Phase E) remain the sole message-flow log, matching every other client. Confidence: high — traced call graph end to end (mqtt_client.cpp:80 wires `component_.setPublishCallback` to `this->publish`, which calls `traceMessage(Outgoing, ...)`; `Rs485InterfaceComponent::publishMappedMessages` calls that same `publishCallback_` and then separately calls its own `logOutgoingMessageIfEnabled`). Proceeding directly to fix (step 7) per user-confirmed scope; step 5/6 tracing not needed since root cause is already proven by the failing unit test.

## resolution

Root cause: `loadRs485InterfaceClientRuntimeConfigFromIni` in
`src/yaha/rs485_interface_client/rs485_interface_client_app.cpp` force-enabled
`mqttConfig.enableMessageTrace` whenever `rs485interface.logIncomingMessages`/`logOutgoingMessages`
was set. This turned on `mqtt_client`'s own generic wire-level message trace
(`mqtt_client.cpp` `traceMessage`) as an unrelated side effect, so every MQTT message
got logged twice in the same process: once as `mqtt_client -> ...`/`mqtt_client <- ...`
(generic transport trace) and once as `rs485_interface -> ...`/`rs485_interface <- ...`
(the component's own message-flow log, added by `spec/yaha/IMPL-message-object-unification.md`
Phase E). No other YAHA client does this auto-enable; it was leftover from before
`rs485_interface` had its own message-flow logging.

Proof: unit test `rs485_runtime_config_does_not_force_mqtt_client_message_trace`
(`src/yaha/rs485_interface_client/test/rs485_interface_client_config_test.cpp`) failed
before the fix (`enableMessageTrace == true` for: `true == false`... i.e. asserted-false
value was `true`) and passes after.

Fix: removed the `if (parsed.rs485Config.logIncomingMessages || parsed.rs485Config.logOutgoingMessages) { parsed.mqttConfig.enableMessageTrace = true; }`
block in `rs485_interface_client_app.cpp`. `mqttConfig.enableMessageTrace` now stays at
its struct default (`false`), matching every other YAHA client. The RS485 wire-level
trace (`rs485interface.trace`) and the component's own MQTT message-flow log
(`logIncomingMessageIfEnabled`/`logOutgoingMessageIfEnabled`) are untouched, per
user-confirmed scope.

Files touched:
- `src/yaha/rs485_interface_client/rs485_interface_client_app.cpp` (fix)
- `src/yaha/rs485_interface_client/SPEC.md` (doc update)
- `src/yaha/rs485_interface_client/test/rs485_interface_client_config_test.cpp` (test:
  removed stale assertion that codified the bug, added regression test)

Verification:
- `python3 test/run_coverage_clients.py` — 1169/1169 tests passed, coverage threshold
  met (all changed production files >= 80% Regions/Functions/Lines).

Test location: already in the standard suite
(`src/yaha/rs485_interface_client/test/rs485_interface_client_config_test.cpp`,
test case `rs485_runtime_config_does_not_force_mqtt_client_message_trace`); no
separate repro file was created, so nothing to move.

No `BUG-TRACE-TEMP` markers were added (root cause proven directly via config-level
unit test, no runtime tracing needed).
