## user report

es sieht für mich danach aus, als hätte ich den selben bug in remote-service. dort finde ich auch zwei mal logging einen mqtt: mqtt_client und einen remote_service -> bitte verfahre hier genauso, dass nur noch das standard logging bleibt, also das logging, dass über eine zentrale gemeinsame klasse läuft

## scope

Same mechanism as `spec/bug/rs485-duplicate-message-log/bug.md`, reported by the user as
"the same bug" in `remote_service`. Scope locked by direct analogy to that already-resolved
bug plus this report's explicit statement of intent ("nur noch das standard logging bleibt,
das über eine zentrale gemeinsame klasse läuft" = keep only the message-flow log that runs
through the shared `message_log_service`, delete the mqtt_client-trace sonder-code).

Allow:
- `src/yaha_remoteserviceclient_main.cpp` — the `mqttConfig.enableMessageTrace` force-enable
  side effect of `remoteservice.logIncomingMessages`/`logOutgoingMessages`.

Deny:
- `RemoteServiceComponent::logOutgoingMessageIfEnabled` (component's own MQTT message-flow
  log via shared `message_log_service`) — stays as-is, this is the "standard logging via a
  central shared class" the user wants to keep.
- Any other client's main.cpp (handled as a separate follow-up, see below).
- `mqtt_client.cpp` generic trace mechanism itself (unchanged; only the remote_service-side
  forced activation is removed).

## confirmed facts

- `src/yaha_remoteserviceclient_main.cpp:154-161`:
  ```cpp
  const bool enableMessageLogsFromConfig =
      runtimeConfig.logIncomingMessages || runtimeConfig.logOutgoingMessages;
  runtimeConfig.mqttConfig.enableMessageTrace =
      cliOptions.enableMessageTrace || enableMessageLogsFromConfig;
  ```
  forces `mqtt_client`'s generic wire-level message trace on whenever
  `remoteservice.logIncomingMessages`/`logOutgoingMessages` is set in the ini — exact same
  bug shape as the already-fixed `rs485_interface_client` case (see
  `spec/bug/rs485-duplicate-message-log/bug.md`).
- `RemoteServiceComponent::logOutgoingMessageIfEnabled`
  (`src/yaha/remote_service/remote_service_component.cpp:458`) already emits the component's
  own message-flow log line (`remote_service -> ...`) via the shared `message_log_service`
  (`buildMessageLogLine`), gated by `remoteServiceConfig.logOutgoingMessages` — this is wired
  from the same ini keys and fires for the same message, producing the reported duplicate.
- No test exists for `yaha_remoteserviceclient_main.cpp` logic (main-entry files are excluded
  from the shared library / coverage build target in `CMakeLists.txt`, same as every other
  `yaha_*client_main.cpp`) — this matches the pattern of the "clean" clients (filestore,
  automation, valueservice, zwave), which only ever do
  `runtimeConfig.mqttConfig.enableMessageTrace = cliOptions.enableMessageTrace;` in main.cpp
  with no dedicated test either. No automated repro/regression test is feasible for this file;
  fix verified by code inspection against the already-proven rs485 mechanism plus a manual
  runtime check.
- Grep sweep while locating this instance found the identical pattern (ini flags OR-ed into
  `mqttConfig.enableMessageTrace`) also present in `yaha_pushoverclient_main.cpp` and
  `yaha_opensensemapclient_main.cpp`. Flagged to user, who confirmed fixing both the same
  way — see addendum below.
- Confirmed clean (no bug, no change needed): `yaha_filestoreclient_main.cpp`,
  `yaha_automationclient_main.cpp`, `yaha_valueserviceclient_main.cpp`,
  `yaha_zwaveclient_main.cpp`, `yaha_serialdeviceclient_main.cpp`,
  `yaha_msgstoreclient_main.cpp` — all set `mqttConfig.enableMessageTrace` only from the
  explicit `--trace-messages` CLI flag, never from ini-driven component logging flags.

## addendum: sibling fixes (pushover, opensensemap)

Same root cause, same fix, applied after explicit user confirmation:

- `src/yaha_pushoverclient_main.cpp` — removed
  `enableMessageTraceFromConfig = logIncomingMessages || logOutgoingMessages` OR-in.
  `mqttConfig.enableMessageTrace` now driven only by `cliOptions.enableMessageTrace`.
  `PushoverComponent`'s own incoming message log (via `message_log_service`, component name
  `pushover`) is untouched.
- `src/yaha_opensensemapclient_main.cpp` — removed
  `enableIncomingLogsFromConfig = logIncomingMessages` OR-in.
  `mqttConfig.enableMessageTrace` now driven only by `cliOptions.enableMessageTrace`.
  `OpenSenseMapComponent`'s own incoming message log (via `message_log_service`, component
  name `opensensemap`) is untouched.

Verification: `python3 test/run_coverage_clients.py` full client suite re-run after both
changes (main.cpp files not part of coverage target, no test regression possible from these
files; full suite pass count re-confirmed).

## test case

No automated unit test is feasible (main.cpp is not part of any test binary). Verification:
manual runtime check — start `yaharemoteserviceclient` with `remoteservice.logOutgoingMessages=true`
in the ini, trigger one outgoing message, confirm only one log line (`remote_service -> ...`)
appears, not two.

## resolution

Root cause: identical to `rs485-duplicate-message-log` — `yaha_remoteserviceclient_main.cpp`
forced `mqttConfig.enableMessageTrace` on as a side effect of the component's own
`logIncomingMessages`/`logOutgoingMessages` ini flags, duplicating the message-flow log
`RemoteServiceComponent` already emits via `message_log_service`.

Fix: removed the `enableMessageLogsFromConfig` OR-in `yaha_remoteserviceclient_main.cpp`.
`mqttConfig.enableMessageTrace` is now driven only by the explicit `--trace-messages` CLI
flag, matching every "clean" client (filestore, automation, valueservice, zwave, and the
already-fixed rs485_interface). `RemoteServiceComponent`'s own message-flow log is untouched.

Files touched:
- `src/yaha_remoteserviceclient_main.cpp` (fix)

Verification: `python3 test/run_coverage_clients.py` — 1169/1169 tests passed, coverage
threshold met (main.cpp not part of coverage target, no regression expected/possible from
this file; verified no other file's coverage regressed).
