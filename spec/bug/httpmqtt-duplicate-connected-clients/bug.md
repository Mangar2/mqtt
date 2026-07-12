## user report

ich finde im httpmqtt client bei open connections eine liste von immer gleichen client namen. ein broker soll sofern ich das richtig erinnere einen alten connect automatisch disconnecten wenn ein client mit der selben id reconnected. Der httpmqtt client scheint das nicht korrekt zu machen und den selben client mehrfach zu connecten. Der Broker zeigt im $SYS loggign auch an, dass der client nicht mehrfach connected ist. Bitte korrigiere den httpmqtt client dasss er connections vom selben client nicht mehrfach offen hält: Jul 12 18:34:45 yahapi yahahttpmqttinterfaceclient[11294]: http_mqtt_interface_client[event] connected_clients_report connected_clients=48 client_ids=ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/weather2,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion,ESP8266/LR/motion

## test case

Standard test suite: `src/yaha/http_mqtt_interface_client/test/http_mqtt_session_manager_test.cpp`.

Added test case `http_mqtt_session_manager_reconnect_same_clientid_supersedes_old_session`
asserting that calling `HttpMqttSessionManager::connect()` twice with the same `clientId`
must tear down (disconnect) the first session and leave exactly one session for that
`clientId` in `listSessions()`, with the old tokens invalidated.

Run command:

```
python3 test/run_coverage_clients.py --unit-only
```

Result before fix (FAILS, demonstrates bug — fix line temporarily commented out with
`// BUG-TRACE-TEMP httpmqtt-duplicate-connected-clients` marker, then restored):

```
[FAILED] Client unit tests
  failing test        : http_mqtt_session_manager_reconnect_same_clientid_supersedes_old_session
  exit                : 42

  --- relevant output ---
  src/yaha/http_mqtt_interface_client/test/http_mqtt_session_manager_test.cpp:135: FAILED: *disconnectCallCount == 1 for: 0 == 1
  test cases: 1 | 1 failed
  assertions: 3 | 2 passed | 1 failed
```

## scope

Allow:
- `src/yaha/http_mqtt_interface_client/http_mqtt_session_manager.h` / `.cpp` —
  session bookkeeping in `connect()`/`disconnect()` and the maps
  `sessionsBySendToken_`, `sessionsByReceiveToken_`, `sendTokenByClientId_`.
- `src/yaha/http_mqtt_interface_client/test/http_mqtt_session_manager_test.cpp` —
  regression test for reconnect-with-same-clientId behavior.

Deny:
- Broker's own client-id takeover / `$SYS` reporting — user confirms this already
  works correctly (broker shows only one connection per client id). Not touched.
- `src/yaha/http_mqtt_interface_client/internal/http_mqtt_interface_client_app_helpers.cpp`
  (`logConnectedClientsReport`) — it only renders whatever `listSessions()` returns;
  it is not the source of the duplication and stays as-is.
- `src/yaha/mqtt_client/mqtt_client.cpp` / `broker_transport.cpp` — the actual MQTT
  transport/TCP layer; untouched, its `isConnected()` behavior is not modified.
- Any other YAHA client module.

## confirmed facts

- Broker `$SYS` logging shows only one live connection per client id (broker-side
  duplicate detection/takeover already works correctly).
- The httpmqtt client's own `connected_clients_report` event lists the same client id
  (`ESP8266/LR/motion`) dozens of times simultaneously, growing over time (48 entries
  in the reported log line).
- Expected behavior: reconnecting with the same `clientId` via the httpmqtt client
  should replace/close the previous session, not accumulate duplicate entries.

## hypothesis

`src/yaha/http_mqtt_interface_client/http_mqtt_session_manager.cpp`, function `connect()`
(pre-fix, lines 42-93): when an HTTP session connects with a `clientId` that already has
an active entry in `sendTokenByClientId_`, the code created a brand-new `SessionState`
with fresh tokens and a new broker transport, and only overwrote
`sendTokenByClientId_[clientId]` (old line 87) with the new send token — it never erased
the *previous* `SessionState`'s entries from `sessionsBySendToken_` /
`sessionsByReceiveToken_`, and never called `transport.disconnect()` on the old session.

Nothing else ever cleans up those orphaned entries (`disconnect()` is only invoked by an
explicit HTTP disconnect request, never automatically on supersession), so
`listSessions()` (line 332) keeps returning the old orphaned `SessionState` objects
indefinitely. Each orphan still reports `brokerConnected = true` because its transport's
local `isConnected()` flag is a cached/local state that is never refreshed (no more
`receive()`/`ping()` calls ever reach an abandoned session once the HTTP client switches
to the new token). This exactly matches the reported symptom: the broker itself only
ever keeps one TCP connection per client id (hence `$SYS` is correct), while the
httpmqtt client's local session bookkeeping accumulates stale duplicate entries forever.

Confidence: high — direct source-level proof (deterministic logic bug, not a timing/
runtime race), confirmed by the regression test added in step 2 which reproducibly
fails on the pre-fix code and passes after the fix. Proceeding directly to fix (step 7)
per repo precedent (see `spec/bug/rs485-duplicate-message-log/bug.md`); step 5/6
live tracing not needed since root cause is fully provable from source and a unit test.

## resolution

Root cause: `HttpMqttSessionManager::connect()` in
`src/yaha/http_mqtt_interface_client/http_mqtt_session_manager.cpp` did not tear down
an existing session for a `clientId` before creating a new one on reconnect. It only
overwrote the `clientId -> sendToken` mapping, leaving the old `SessionState` (and its
now-orphaned broker transport) alive in `sessionsBySendToken_`/`sessionsByReceiveToken_`
forever. Since `listSessions()` iterates all entries in `sessionsBySendToken_` and reports
any session whose transport still locally believes it is connected, every reconnect of
an ESP8266 device (e.g. after a Wi-Fi hiccup) added one more permanent duplicate entry
to the `connected_clients_report` event — 48 duplicates of `ESP8266/LR/motion` in the
reported log line — even though the broker correctly kept only one live connection per
client id.

Fix: added `HttpMqttSessionManager::disconnectExistingSessionForClientId()`, called at
the start of `connect()` before creating the new session. It looks up any existing
session for the requested `clientId`, removes it from `sessionsBySendToken_` /
`sessionsByReceiveToken_` / `sendTokenByClientId_` under the session mutex, and then
calls `transport.disconnect()` on it (best-effort, exceptions swallowed) — mirroring the
cleanup already done in `disconnect()`. This guarantees at most one live session per
`clientId` in the manager's bookkeeping, matching broker semantics.

Files touched:
- `src/yaha/http_mqtt_interface_client/http_mqtt_session_manager.h` — added private
  method declaration `disconnectExistingSessionForClientId`.
- `src/yaha/http_mqtt_interface_client/http_mqtt_session_manager.cpp` — implemented the
  method and call it at the top of `connect()`.
- `src/yaha/http_mqtt_interface_client/test/http_mqtt_session_manager_test.cpp` — added
  regression test `http_mqtt_session_manager_reconnect_same_clientid_supersedes_old_session`.

No `BUG-TRACE-TEMP` markers remain (temporary revert used only to confirm the test fails
pre-fix was removed immediately after verification).

Quality gate:

```
python3 test/run_coverage_clients.py
```

Result: `Tests: 1172/1172 [OK]`, threshold MET for all client production files.
Changed file `yaha/http_mqtt_interface_client/http_mqtt_session_manager.cpp` coverage:
Regions 93.29%, Functions 93.75%, Lines 93.23%, Branches 82.56% (all above the 80%
threshold).

Test location: regression test lives directly in the standard suite
(`src/yaha/http_mqtt_interface_client/test/http_mqtt_session_manager_test.cpp`), no
separate `spec/bug/` test file was needed.
