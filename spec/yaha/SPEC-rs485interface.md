# RS485Interface

## Purpose

RS485Interface bridges MQTT commands and status topics to a token-based RS485 serial bus used by microcontroller devices.

It translates MQTT messages to serial protocol frames, manages token ownership on the RS485 bus, reads serial replies/status, and publishes mapped MQTT messages back.

## Role in the system

RS485Interface is a YAHA component behind the runtime boundary from [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md).

It consumes:
- inbound MQTT command topics derived from configured addresses/settings and explicit topic mappings
- serial input bytes from RS485 transport
- periodic internal timer ticks for token-state scheduling

It produces:
- serial protocol frames (token, command, time-of-day broadcast)
- outbound MQTT state/status messages derived from serial messages

## Standalone program structure

Standalone RS485Interface executable contains:
1. Generic YAHA MQTT client runtime.
2. RS485Interface domain component.
3. Serial transport adapter.
4. Tick-based scheduler for token exchange and queued command sending.

Main composition rule:
- main composes MQTT runtime and RS485Interface component.
- MQTT runtime owns broker transport behavior.
- RS485Interface owns serial protocol behavior and topic mapping.

## Subscriptions

`getSubscriptions()` returns map `topicPattern -> qos`.

Mandatory subscriptions are derived from configuration:
1. For each configured address topic prefix and each configured setting suffix:
   - `<address-pattern-with-+>/<setting-path>/set`
   - QoS = configured `qos`
2. For each configured explicit topic in `topics`:
   - `<topic>/+`
   - QoS = configured `qos`
3. System control namespace for RS485Interface control:
   - `$MONITORING/rs485Interface/#`
   - QoS = configured `qos`

Compatibility note:
- Legacy source uses `$SYS/rs485Interface/#` and `$SYS/rs485Interface/trace/set`.
- YAHA spec replaces this namespace by `$MONITORING/rs485Interface/#` and `$MONITORING/rs485Interface/trace/set`.

## Published messages

RS485Interface publishes MQTT messages derived from non-token serial messages.

Publish rules:
- outgoing topic/value come from serial-to-MQTT mapping rules
- outgoing QoS is always configured `qos`
- reason text is preserved by mapper or matched reply updater
- token protocol messages are internal and are not published to MQTT

## External interfaces

Component interface:
- `run()`
- `close()`
- `getSubscriptions() -> map<string, qos>`
- `handleMessage(message)`
- `on("publish", callback)` for outbound MQTT publish callback registration

Serial transport interface:
- open serial port with configured `serialPortName` and `baudrate`
- receive raw byte arrays
- send serialized protocol frames

## Data model

## Serial message frame

Protocol fields:
- sender address
- receiver address
- reply bit
- protocol version
- command byte
- value (word, special float decoding for selected status commands)
- integrity field (parity for v0, CRC16 for v1)

Supported message versions:
- v0 and v1 only
- runtime may downgrade sending version when lower bus participant version is detected

## Token-state model (RS485State)

State set:
- `STATE_UNKNOWN`
- `STATE_REBOOT`
- `STATE_SINGLE`
- `STATE_UNREGISTERED`
- `STATE_REGISTERED`

State update input classes:
- token events: `ENABLE_SEND`, `REGISTRATION_INFO`, `REGISTRATION_REQUEST`
- timer loop events: `LOOP_START`, `LOOP_SHORT_BREAK`, `LOOP_LONG_BREAK`, `LOOP_TIMEOUT`

State output classes:
- `STATE_UNCHANGED`
- `STATE_CHANGED`
- token emits: `ENABLE_SEND`, `REGISTRATION_INFO`, `REGISTRATION_REQUEST`

State side effects:
- `maySend` flag
- right sibling and leftmost sibling tracking
- timer and last-enable-send tracking for token-loss behavior

## Queue and action model

- Outbound serial command queue supports replacement semantics for same sender/receiver/command except command `X`.
- MQTT command suffix behavior:
  - `/set` -> direct set
  - `/temporary` -> delayed auto-off sequence
  - `/blink` -> repeated toggle sequence
- Last observed MQTT state per topic is cached for blink phase decisions.

## Behavior

## Startup and runtime

On `run()`:
1. Register serial receive callback.
2. Open serial interface with retry policy.
3. Start scheduler loop.
4. Start periodic time-of-day broadcast (`command='C'`, broadcast receiver).

## Inbound serial processing

For each parsed serial message:
1. Feed token exchange state machine.
2. If token exchange returns protocol reply, enqueue/send reply over serial.
3. If message is non-token, convert to MQTT message list and publish.
4. Update action state cache from published MQTT messages.

Noise handling:
- leading invalid/noise bytes (0 or address > 127) are skipped before message decode.

## Inbound MQTT processing

For each `handleMessage(message)`:
1. If topic is `$MONITORING/rs485Interface/trace/set`, update trace level.
2. Otherwise:
   - annotate reason (`received by RS485Interface service` compatibility string)
   - process action semantics (`/set`, `/temporary`, `/blink`)
   - map each resulting action message to serial frame
   - mark serial frame `reply=true`
   - enqueue serial frame for scheduler send

## Scheduler behavior

Per tick:
1. Advance token state without inbound message and send any resulting token frame.
2. If `maySend` is true, send queue head (with retry and dequeue rules).
3. Reset `maySend` after queue attempt to avoid double-send in same tick.

## Persistence

No durable persistence is required by RS485Interface domain logic.

Runtime-only state:
- token state machine internals
- send queue content
- transient action retry/timer state
- cached per-topic last values

## Configuration

Required/optional configuration contract:

Core transport and timing:
- `serialPortName: string` (required)
- `baudrate: integer` (default `57600`)
- `myAddress: integer` in `[1,127]` (default `1`)
- `maxVersion: 0|1|2` (default `1`)
- `tickDelay: integer` (default `100`)
- `timeOfDayDelayInSeconds: integer` (default `60`)

MQTT and tracing:
- `qos: 0|1|2` (default `1`)
- `trace: "errors"|"messages"|"internal"` (default `"messages"`)

Action behavior:
- `blinkDelayInSeconds: integer` (default `3`)
- `temporaryOnInSeconds: integer` (default `1200`)

Topic and protocol mapping:
- `interfaces: object` (string-to-int command maps)
- `settings: object` (serial command -> setting topic suffix)
- `status: object` (serial command -> status topic suffix)
- `addresses: object` (topic prefix -> serial address)
- `topics: object` (optional direct topic -> {command,value,address} mapping)

## Error handling

- Serial open/send failures: retry and reopen path; log error context.
- Serial decode errors (CRC/parity/length/data): report as trace error entry.
- Unknown mapping elements (topic/address/command/value): throw in mapper and log.
- Runtime continues where possible after recoverable per-message errors.

## Architectural notes

- RS485 token protocol behavior is concentrated in `RS485State` + `RS485TokenExchange`.
- MQTT transport behavior must stay outside component in generic runtime.
- RS485Interface domain behavior must remain equivalent to legacy JS source behavior.

## Mandatory parity requirement for new C++ client

This section is normative and release-blocking.

The new C++ RS485Interface client must behave exactly like legacy `rs485state.js` in every observable and internal state-machine aspect, with zero exceptions.

No deviation is allowed in:
- state transitions
- timer-loop transition triggers
- token-loss detection timing
- emitted token event decisions
- `maySend` transitions
- sibling tracking side effects
- reset/timeout behavior
- all return values for all input combinations

The allowed tolerance is exactly zero. "Almost equal" is forbidden.

## Mandatory unit-test gate for parity

Development is incomplete without a gapless unit-test suite proving full parity against legacy `rs485state.js`.

Required test gate:
1. A complete transition matrix test covering every RS485 state, every input class, and all relevant preconditions (`timer`, `lastEnableSend`, `rightSibling`, `leftmostSibling`, `maySend`, addressed/not-addressed).
2. Golden-reference comparison tests against legacy `rs485state.js` outputs and side effects for identical input sequences.
3. Deterministic replay tests for long mixed event sequences (message/no-message/timer events) to prove no drift.
4. Explicit negative assertion: no test may accept any behavioral delta, not even a single differing field or tick.

Acceptance rule:
- If one parity test differs at one step, implementation is rejected.
- No waiver process exists for parity deviations.

## Open questions

- None.
