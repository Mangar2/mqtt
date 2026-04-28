# MQTT Test Client – Implementation Plan

This document contains the full mqttx compatibility audit for the test client and the ordered implementation roadmap to close all identified gaps.

Scope constraints:
- Goal: best possible option and behavior parity with mqttx.
- TLS/secure options are explicitly out of scope (`--key`, `--cert`, `--ca`, `--insecure`, `--alpn`, mqtts/wss behavior).

Status vocabulary:
- implemented: option/command is available and behavior is aligned with mqttx intent for the supported scope.
- wrongly implemented: option/command is parsed or partially available, but runtime behavior diverges from mqttx semantics.
- not implemented: option/command is missing.

## Appendix A – MQTTX Option Inventory (All Commands)

Source of truth captured on 2026-04-28:
- `spec/mqttx-all-help.txt`

MQTTX top-level commands:
- `check` - not implemented
- `init` - not implemented
- `conn` - not implemented
- `pub` - implemented
- `sub` - not implemented
- `bench` (`bench conn`, `bench pub`, `bench sub`) - wrongly implemented (core semantic gaps vs mqttx, especially `bench pub` connection model)
- `simulate` - not implemented
- `ls` - not implemented

Top-level options:
- `-v`, `--version` - not implemented - meaning: print CLI version.
- `-h`, `--help` - not implemented - meaning: show command help.

Global connection/session/security options used by `conn`, `pub`, `sub`, `bench *`, `simulate`:
- `-V`, `--mqtt-version <5.0/3.1.1/3.1>` - wrongly implemented - meaning: mqttx supports 5.0/3.1.1/3.1; current implementation rejects non-5 values.
- `-h`, `--hostname <HOST>` - implemented - meaning: broker host address.
- `-p`, `--port <PORT>` - implemented - meaning: broker port.
- `-i`, `--client-id <ID>` - implemented - meaning: client identifier.
- `-I`, `--client-id <ID>` (bench/simulate variants with `%i` support) - implemented - meaning: client identifier template; `%i` is replaced by index.
- `--no-clean` - implemented - meaning: set clean session/clean start to false.
- `-k`, `--keepalive <SEC>` - implemented - meaning: keepalive ping interval in seconds.
- `-u`, `--username <USER>` - implemented - meaning: MQTT username.
- `-P`, `--password <PASS>` - implemented - meaning: MQTT password.
- `-l`, `--protocol <PROTO>` - implemented - meaning: transport protocol (`mqtt`, `mqtts`, `ws`, `wss` in mqttx; here non-TLS only).
- `--path <PATH>` - implemented - meaning: WebSocket path.
- `-wh`, `--ws-headers <WSHEADERS...>` - implemented - meaning: additional WebSocket HTTP headers.
- `--key <PATH>` - not implemented - meaning: TLS client private key path.
- `--cert <PATH>` - not implemented - meaning: TLS client certificate path.
- `--ca <PATH>` - not implemented - meaning: TLS CA certificate path.
- `--insecure` - not implemented - meaning: disable TLS server certificate verification.
- `--alpn <PROTO...>` - not implemented - meaning: ALPN protocol list for TLS handshake.
- `-rp`, `--reconnect-period <MILLISECONDS>` - wrongly implemented - meaning: parsed, but mqttx-style reconnect behavior is not consistently applied in one-shot `pub` and current bench runtime paths.
- `--maximum-reconnect-times <NUMBER>` - wrongly implemented - meaning: parsed, but mqttx-style reconnect behavior is not consistently applied in one-shot `pub` and current bench runtime paths.
- `--maximun-reconnect-times <NUMBER>` (as printed by `mqttx simulate --help`) - not implemented - meaning: mqttx simulate alias/spelling variant for max reconnect attempts.
- `-se`, `--session-expiry-interval <SECONDS>` - implemented - meaning: session expiry interval.
- `--rcv-max`, `--receive-maximum <NUMBER>` - implemented - meaning: MQTT 5 receive maximum.
- `--maximum-packet-size <NUMBER>` - implemented - meaning: maximum packet size client accepts.
- `--topic-alias-maximum <NUMBER>` - implemented - meaning: topic alias maximum value.
- `--req-response-info` - implemented - meaning: request response information from server.
- `--no-req-problem-info` - implemented - meaning: disable requesting problem information.
- `-up`, `--user-properties <USERPROPERTIES...>` - implemented - meaning: MQTT 5 user properties for current packet context.
- `-Cup`, `--conn-user-properties <USERPROPERTIES...>` - implemented - meaning: MQTT 5 CONNECT user properties.
- `-Wt`, `--will-topic <TOPIC>` - implemented - meaning: will topic.
- `-Wm`, `--will-message <BODY>` - implemented - meaning: will payload.
- `-Wq`, `--will-qos <0/1/2>` - implemented - meaning: will QoS level.
- `-Wr`, `--will-retain` - implemented - meaning: send will as retained message.
- `-Wd`, `--will-delay-interval <SECONDS>` - implemented - meaning: will delay interval.
- `-Wpf`, `--will-payload-format-indicator` - implemented - meaning: will payload UTF-8 format indicator.
- `-We`, `--will-message-expiry-interval <SECONDS>` - implemented - meaning: will message expiry interval.
- `-Wct`, `--will-content-type <CONTENTTYPE>` - implemented - meaning: will content type metadata.
- `-Wrt`, `--will-response-topic <TOPIC>` - implemented - meaning: will response topic metadata.
- `-Wcd`, `--will-correlation-data <DATA>` - implemented - meaning: will correlation data metadata.
- `-Wup`, `--will-user-properties <USERPROPERTIES...>` - implemented - meaning: will user properties.
- `-so`, `--save-options [PATH]` - wrongly implemented - meaning: currently parsed/consumed but no effective save behavior in mqttx-compatible command paths.
- `-lo`, `--load-options [PATH]` - wrongly implemented - meaning: currently parsed/consumed but no effective load behavior in mqttx-compatible command paths.
- `-am`, `--authentication-method <METHOD>` - implemented - meaning: MQTT 5 enhanced auth method.
- `--debug` (not present on every command) - wrongly implemented - meaning: currently accepted but has no runtime effect.
- `--help` - wrongly implemented - meaning: available for top-level and `pub`, but not fully mirrored for all mqttx-compatible bench subcommand help flows.

Publish-oriented options (`pub`, `bench pub`, `simulate`):
- `-t`, `--topic <TOPIC>` - implemented - meaning: publish topic name (templates may support `%u`, `%c`, `%i` depending on command).
- `-m`, `--message <BODY>` - implemented - meaning: publish payload text.
- `-q`, `--qos <0/1/2>` - implemented - meaning: publish QoS level.
- `-r`, `--retain` - implemented - meaning: set retained flag.
- `-d`, `--dup` - wrongly implemented - meaning: effective for `pub`, but ignored in `bench pub` runtime path.
- `-pf`, `--payload-format-indicator` - wrongly implemented - meaning: effective for `pub`, but ignored in `bench pub` runtime path.
- `-e`, `--message-expiry-interval <NUMBER>` - wrongly implemented - meaning: effective for `pub`, but ignored in `bench pub` runtime path.
- `-ta`, `--topic-alias <NUMBER>` - wrongly implemented - meaning: effective for `pub`, but ignored in `bench pub` runtime path.
- `-rt`, `--response-topic <TOPIC>` - wrongly implemented - meaning: effective for `pub`, but ignored in `bench pub` runtime path.
- `-cd`, `--correlation-data <DATA>` - wrongly implemented - meaning: effective for `pub`, but ignored in `bench pub` runtime path.
- `-si`, `--subscription-identifier <NUMBER>` - wrongly implemented - meaning: effective for `pub`, but ignored in `bench pub` runtime path.
- `-ct`, `--content-type <TYPE>` - wrongly implemented - meaning: effective for `pub`, but ignored in `bench pub` runtime path.
- `-v`, `--verbose` (in bench/simulate) - implemented - meaning: print history/statistics with rates.

`pub`-only payload/source/encoding options:
- `-s`, `--stdin` - implemented - meaning: read payload from stdin.
- `-M`, `--multiline` - implemented - meaning: read stdin line-by-line as multiple messages.
- `-lm`, `--line-mode` - implemented - meaning: interactive line mode (`-s -M` semantics).
- `-f`, `--format <TYPE>` - implemented - meaning: input payload format (for example `base64`, `json`, `hex`, `binary`, `cbor`, `msgpack`).
- `--file-read <PATH>` - implemented - meaning: read payload from file.
- `-Pp`, `--protobuf-path <PATH>` - wrongly implemented - meaning: currently accepted but not used to build/encode payload.
- `-Pmn`, `--protobuf-message-name <NAME>` - wrongly implemented - meaning: currently accepted but not used to build/encode payload.
- `-Ap`, `--avsc-path <PATH>` - wrongly implemented - meaning: currently accepted but not used to build/encode payload.
- `-S`, `--payload-size <SIZE>` - wrongly implemented - meaning: currently accepted but no random payload generation is applied.

Subscribe-oriented options (`sub`, `bench sub`):
- `-t`, `--topic <TOPIC...>` - implemented - meaning: one or more subscribe topic filters.
- `-q`, `--qos <0/1/2...>` - wrongly implemented - meaning: accepted for `bench sub`, but current runtime uses fixed subscribe QoS settings.
- `-nl`, `--no_local [FLAG...]` - wrongly implemented - meaning: accepted for `bench sub`, but current runtime does not apply no-local behavior to SUBSCRIBE packets.
- `-rap`, `--retain-as-published [FLAG...]` - wrongly implemented - meaning: accepted for `bench sub`, but current runtime does not apply retain-as-published behavior.
- `-rh`, `--retain-handling <0/1/2...>` - wrongly implemented - meaning: accepted for `bench sub`, but current runtime does not apply retain-handling behavior.
- `-si`, `--subscription-identifier <NUMBER...>` - wrongly implemented - meaning: accepted for `bench sub`, but current runtime does not apply subscription identifier as mqttx does.
- `-f`, `--format <TYPE>` (`sub`) - not implemented - meaning: decode/format incoming message payload.
- `-v`, `--verbose` - wrongly implemented - meaning: in bench flows it currently toggles metrics-json style output instead of mqttx-style verbose subscribe history semantics.
- `--output-mode <default/clean>` (`sub`) - not implemented - meaning: choose default or clean output style.
- `--file-write <PATH>` (`sub`) - not implemented - meaning: append incoming messages to one file.
- `--file-save <PATH>` (`sub`) - not implemented - meaning: save each incoming message to separate file(s).
- `--delimiter [CHARACTER]` (`sub`) - not implemented - meaning: append delimiter between output messages.
- `-Pp`, `--protobuf-path <PATH>` (`sub`) - not implemented - meaning: `.proto` path for payload decoding.
- `-Pmn`, `--protobuf-message-name <NAME>` (`sub`) - not implemented - meaning: protobuf type name for decoding.
- `-Ap`, `--avsc-path <PATH>` (`sub`) - not implemented - meaning: AVRO schema path for decoding.

Benchmark-control options:
- `-c`, `--count <NUMBER>` - wrongly implemented - meaning: should be number of persistent benchmark connections; current implementation uses it mainly as slot/index count while `bench pub` still reconnects per publish operation.
- `-i`, `--interval <MILLISECONDS>` - wrongly implemented - meaning: parsed for `bench pub`, but publish-rate runtime currently uses message interval, not connect interval semantics.
- `-im`, `--message-interval <MILLISECONDS>` (`bench pub`) - implemented - meaning: delay between publish operations.
- `-L`, `--limit <NUMBER>` (`bench pub`) - wrongly implemented - meaning: in mqttx `0` means unlimited; current implementation maps `0` to a bounded operation count path.
- `--split [CHARACTER]` (`bench pub`) - wrongly implemented - meaning: currently parsed/consumed but not applied in publish runtime behavior.
- `-S`, `--payload-size <SIZE>` (`bench pub`) - wrongly implemented - meaning: currently parsed/consumed but not applied in publish runtime behavior.

Simulation-specific options (`simulate`):
- `-sc`, `--scenario <SCENARIO>` - not implemented - meaning: run built-in simulation scenario name.
- `-f`, `--file <SCENARIO FILE PATH>` - not implemented - meaning: run simulation script from file.
- `-c`, `--count <NUMBER>` - not implemented - meaning: simulation connection count.
- `-i`, `--interval <MILLISECONDS>` - not implemented - meaning: simulation connect interval.
- `-im`, `--message-interval <MILLISECONDS>` - not implemented - meaning: simulation publish interval.
- `-L`, `--limit <NUMBER>` - not implemented - meaning: simulation publish limit.
- `-t`, `--topic <TOPIC>` - not implemented - meaning: simulation topic template.

Listing/maintenance command options:
- `ls`: `-sc`, `--scenarios`, `-h`, `--help` - not implemented - meaning: list built-in scenarios/help for listing command.
- `init`: `-h`, `--help` - not implemented - meaning: initialize mqttx option/config file.
- `check`: `-h`, `--help` - not implemented - meaning: check mqttx updates/help.

Note for CLI compatibility tests:
- MQTTX expects `-q 1` (or `--qos 1`).
- `-q1` is not listed as a documented MQTTX flag form, but is implemented as a compatibility convenience in this client.

## Ordered Implementation Work Packages

All work packages below include required integration tests. A work package is only complete when its related integration tests are implemented and passing.

### WP1 – Command Surface and Help Parity (foundation)

Implementation status (2026-04-28): implemented.

Implemented scope:
- Top-level `--help` and `--version`/`-v` are supported.
- mqttx compatibility command stubs `conn`, `sub`, `simulate`, `ls`, `init`, and `check` are recognized with help flow (`--help`).
- `bench --help` and `bench conn|pub|sub --help` are accepted and return help output.

Verification evidence:
- Unit tests: `test_client_cli_wp1_version_flags_are_supported`, `test_client_cli_wp1_stub_commands_help_flow_is_supported`, `test_client_cli_wp1_stub_commands_without_help_fail`, `test_client_cli_wp1_bench_help_flows_are_supported`.
- Integration tests: `test-client-shell/test_client_shell_wp1_command_help_discoverability`, `test-client-shell/test_client_shell_wp1_version_output_contract`, `test-client-shell/test_client_shell_wp1_unknown_behavior_parity`.

Goal:
- Implement missing top-level command shell behavior and help/version parity where in scope.

Main items:
- Add top-level `--help` and `--version` behavior aligned with mqttx.
- Add command stubs and help flows for `conn`, `sub`, `simulate`, `ls`, `init`, `check` (even if command internals are delivered later).
- Fix `--help` parity for bench subcommands.

Integration tests required:
- Command discoverability tests for each command (`--help` exits success, expected sections present).
- Top-level `--version` output contract test.
- Unknown command and unknown option behavior parity tests.

### WP2 – Shared Connection Semantics Parity

Implementation status (2026-04-28): implemented.

Implemented scope:
- One-shot `pub`/`publish` applies reconnect retry semantics with `-rp`/`--reconnect-period` and `--maximum-reconnect-times`.
- Step 32 direct bench operations (`bench pub` / load publish path and direct subscribe path) apply reconnect retry semantics with the same reconnect settings.
- mqttx alias spelling `--maximun-reconnect-times` is accepted and mapped to `maximum_reconnect_times` in mqttx-compatible parser paths.
- Recognized but not implemented mqttx options `--debug`, `--save-options`, and `--load-options` are now explicitly rejected with clear argument errors in mqttx-compatible paths.

Verification evidence:
- Unit tests: `test_client_cli_wp2_reconnect_alias_maximun_is_supported`, `test_client_cli_wp2_pub_rejects_not_implemented_debug_save_load_options`, `test_client_cli_wp2_bench_rejects_not_implemented_debug_save_load_options`.
- Integration tests: `test-client-shell/test_client_shell_wp2_pub_reconnect_matrix`, `test-client-shell/test_client_shell_wp2_bench_reconnect_matrix`, `test-client-shell/test_client_shell_wp2_reconnect_alias_precedence`.

Goal:
- Make shared connection/session behavior consistent across commands that already parse these options.

Main items:
- Implement mqttx-compatible reconnect behavior for `-rp` and `--maximum-reconnect-times`.
- Add simulate alias support `--maximun-reconnect-times` where required for compatibility.
- Keep non-TLS constraint explicit while preserving protocol parsing parity.
- Ensure debug/save/load options are either fully functional or explicitly rejected with clear error if out of scope.

Integration tests required:
- Reconnect matrix tests (`0`, finite, and edge values) for `pub` and bench flows.
- Session/connect option propagation tests (connect packet property assertions).
- Compatibility tests for alias spelling and option precedence.

### WP3 – Bench Runtime Semantic Alignment

Goal:
- Close the largest semantic gap: `bench` runtime behavior vs mqttx intent.

Main items:
- Rework `bench pub` to persistent connection model instead of reconnect-per-message.
- Fix semantics for `--count`, `--interval`, `--limit` (`0` means unlimited), `--split`, and bench `-S` payload size.
- Align bench verbose output semantics with mqttx.

Integration tests required:
- Long-running bench integration tests validating connection reuse and throughput.
- Bench control option semantic tests (`count`, `interval`, `message-interval`, `limit`, `split`, payload-size).
- Output contract tests for verbose and metrics outputs.

### WP4 – Publish Feature Completion (pub and bench pub)

Goal:
- Ensure publish-related flags have real runtime effect in all applicable modes.

Main items:
- Apply publish property options in bench runtime (`-d`, `-pf`, `-e`, `-ta`, `-rt`, `-cd`, `-si`, `-ct`).
- Implement payload generation/encoding features currently parsed only (`-Pp`, `-Pmn`, `-Ap`, `-S`).
- Verify consistency between one-shot `pub` and bench publish path.

Integration tests required:
- End-to-end publish property validation against broker-observed packet properties.
- Payload format tests (base64/json/hex/binary/cbor/msgpack/protobuf/avro when enabled).
- Regression tests for stdin/multiline/line-mode/file-read interactions.

### WP5 – Subscribe Feature Completion (sub and bench sub)

Goal:
- Implement full subscribe path and align subscription option semantics.

Main items:
- Deliver `sub` command runtime and output pipeline parity.
- Implement effective behavior for `-q`, `-nl`, `-rap`, `-rh`, `-si` in applicable subscribe modes.
- Implement `sub` output and decoding options (`-f`, `--output-mode`, `--file-write`, `--file-save`, `--delimiter`, protobuf/avro decode inputs).

Integration tests required:
- Multi-topic subscribe tests with per-topic QoS and retain behavior assertions.
- Packet-level option application tests (no-local, retain handling, subscription identifier).
- Output path tests (stdout modes, delimiter, file write/save behavior).

### WP6 – Simulate and Maintenance Commands

Goal:
- Deliver missing non-bench command families for mqttx-compatible workflows.

Main items:
- Implement `simulate` command options and scenario execution surface.
- Implement `ls --scenarios` listing behavior.
- Implement in-scope behavior for `init` and `check` command families.

Integration tests required:
- Scenario execution tests for inline and file-based simulation definitions.
- Scenario listing contract tests.
- Initialization/check command behavior tests including exit codes.

### WP7 – End-to-End Parity Gate and Documentation Lock

Goal:
- Stabilize compatibility and prevent regressions.

Main items:
- Build a parity test matrix for all supported mqttx options in scope.
- Enforce status correctness (`implemented`, `wrongly implemented`, `not implemented`) with evidence.
- Keep this document and command help artifacts synchronized.

Integration tests required:
- Full parity sweep tests generated from option inventory categories.
- Cross-command consistency tests for shared options.
- CI gate: fail if any previously passing parity case regresses.

## Delivery Order

Execution order is mandatory:
1. WP1
2. WP2
3. WP3
4. WP4
5. WP5
6. WP6
7. WP7

Rationale:
- WP1 and WP2 establish the command and shared option foundation.
- WP3 resolves the highest semantic risk area first.
- WP4 and WP5 complete publish/subscribe runtime correctness.
- WP6 adds remaining command families.
- WP7 freezes parity with automated regression protection.
