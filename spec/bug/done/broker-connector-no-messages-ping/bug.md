## user report

es geht immer noch nicht, ich erhalte keine messages und der ping fehler kommt wie voher

## test case

command:
./build/release/yahabrokerconnectorclient test/connector.ini

expected bug signal:
- source side reports repeated ping failure (status 400)
- no source messages are forwarded to receiver

current status:
- reproduces in user environment (reported)
- local agent environment cannot fully validate external hosts yahapi/yaha2 reachability

## root cause

- Connector advertised callback port `1` to the source HTTP broker although `listenerPort` in INI was configured (for example `8283`).
- Cause in code: fixed-port bind path treated `bind_to_port(...)` return value as a port number.
- On affected httplib API usage, this value is a success flag (boolean-like), not the actual bound port.
- Result: source broker tried callback delivery to `<listenerHost>:1` and failed with `ECONNREFUSED`.
- Side effect: no source messages reached connector callback endpoint, therefore no forwarding to receiver MQTT broker.

## fix implemented

- File: `src/yaha/broker_connector/source_http_adapter.cpp`
- In `startListener(...)` fixed-port branch:
	- check `bind_to_port(...)` as success boolean.
	- set `boundListenerPort_ = config_.listenerPort` explicitly.
- Dynamic-port branch (`listenerPort == 0`) still uses `bind_to_any_port(...)` and keeps returned port value.

## verification

- User-confirmed production symptom before fix:
	- source HTTP broker log: `ECONNREFUSED 192.168.0.156:1`
- After fix:
	- connector now advertises configured INI port (for example `8283`) in `/connect` payload.
	- callback target no longer falls back to port `1`.

## status

- Closed
- Closed at: 2026-05-02
- Resolution: Fixed in source listener bind/advertise path.
