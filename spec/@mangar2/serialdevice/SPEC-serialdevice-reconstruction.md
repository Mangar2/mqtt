# SPEC SerialDevice 1:1 Reconstruction for YAHA Client

## 1. Purpose and Scope

This specification defines the exact runtime behavior required to reconstruct the legacy `@mangar2/serialdevice` module as a new YAHA client with behavior parity.

Goal:
- Rebuild functionality 1:1, including data formats, control flow, retries, mappings, side effects, and known quirks.

In scope:
- Serial line input parsing to internal message model.
- Internal message model mapping to MQTT publish messages.
- MQTT input mapping to outgoing serial payloads.
- Configuration schema/defaults and topic/address/value maps.
- Subscribe derivation logic.
- Runtime service lifecycle (open/run/close/keep-alive/retry).
- Trace and error handling behavior.

Out of scope:
- Re-design, optimization, or semantic cleanup.
- Protocol extensions not present in the legacy implementation.

## 2. Source Baseline

Behavior is reconstructed from:
- `index.js`
- `serialdevice.js`
- `configuration.js`
- `parseserialdata.js`
- `serialtomqtt.js`
- `mqtttoserial.js`
- `serialmessagetostring.js`
- `serialmessage.js`
- `derivesubscribes.js`
- `constants.js`
- `README.md`
- `CHANGELOG.md`
- test runners in `test/**`

## 3. Runtime Architecture

## 3.1 Public Entry Points

1. `prepare(config, serialDevice = null)`
- If `serialDevice` is falsy, create `new SerialDevice(config)`.
- Return object instance.

2. `SerialDevice` class
- Main service class to bridge MQTT and serial bus.

## 3.2 Internal Components

`SerialDevice` composes:
- `sanitizeConfiguration(options)`
- `Callbacks(['publish'])`
- `MatchMessages`
- `SerialConnection`
- `TaskQueue(delay=100ms)`
- `MQTTToSerial`
- `ParseSerialData`
- `SerialToMQTT`

TaskQueue behavior:
- Outgoing serial messages are queued.
- Queue emits `task` events; each task is sent through `_sendDataToSerial`.
- Delay between tasks is fixed to 100ms (see changelog 1.4.0).

## 4. Data Model

## 4.1 Internal `SerialMessage`

Fields:
- `interfaceName: string`
- `sender: string | number | null`
- `receiver: string | number | null`
- `command: string`
- `value: number | string`
- `action: string` (default `""`, currently `"/set"` for FS20 incoming conversion)

`toString()` format:
- `Interface: <interfaceName> Sender: <sender> Receiver: <receiver> Command: <command> Value: <value>`

No runtime validation in setters. Any value assignment is accepted.

## 4.2 MQTT message contract

Implementation uses `@mangar2/message` with fields:
- `topic`
- `value`
- `reason`
- `qos` (set before publish callback)

Runtime also uses:
- `message.addReason(...)`

## 5. Constants and Bit Semantics

From `constants.js`:
- `SWITCH_ON = 0x4000`
- `SWITCH_OFF = 0x2000`
- `MAX_ADDRESS = 127` (exported but not actively used in this module set)

Switch message value encoding:
- Lower bits identify target switch bit.
- High bits encode command (`ON` or `OFF`).

## 6. Configuration Specification

## 6.1 Sanitization and Validation

`sanitizeConfiguration(config)` behavior:
1. If `config` is not an object:
- call error logger with fatal text
- call `process.exit(1)`

2. Merge `config` with defaults via `sanitize(config, defaultConfiguration, checkConfiguration)`.

3. Validate result against JSON schema (`CheckInput`).

4. Return sanitized configuration.

No custom post-validation normalization except what `sanitize` performs.

## 6.2 Top-level configuration keys

Required by schema:
- `serialPortName: string`
- `baudrate: integer`
- `qos: 0|1|2`
- `trace: "errors"|"messages"|"internal"`
- `interfaces: object`

Optional with defaults in defaults object:
- `baudrate` default `38400`
- `qos` default `1`
- `trace` default `"internal"`
- `keepAliveDelayInSeconds` default `30`

## 6.3 Interfaces object and semantics

Supported interface keys:
- `i2c`
- `fs20`
- `switch`
- `serial`

### 6.3.1 `i2c`

Keys:
- `commandMap: { serialCommand -> topicEnd }`
- `receiverMap: { topicPrefix -> receiverAddress }`

Both are required in schema for `i2c`.

### 6.3.2 `fs20`

Keys:
- `commandMap: { serialCommand -> topicEnd }` required
- `sendMap: { serialCommand -> topicEnd }` optional

Behavior note:
- `sendMap` is used by serial->mqtt topic resolution only when `commandMap` has no match.
- Added to separate receive-only vs send-capable mappings (changelog 1.2.0).

### 6.3.3 `switch`

Keys:
- `topicMap: { mqttTopic -> { command, value, address } }`

Expected content:
- `command` typically `"switch"`
- `value` is switch bit mask
- `address` typically `"main"`

### 6.3.4 `serial`

Keys:
- `commandMap: { serialCommand -> topicEnd }` required
- `receiverMap: { topicPrefix -> receiverAddress }` optional in schema/defaults
- `valueMap: { mapName -> { usedby: string[], map: { mqttValueString -> integer }, description? } }` required

`usedby` entries are single-character commands in schema intent.

## 7. Subscribe Derivation

Implemented by `deriveSubscribes(options)`.

## 7.1 Topic-map subscriptions

For each key `topic` in `topicMap`:
- subscribe to `topic + "/+"` with `qos`.

## 7.2 Command/receiver subscriptions

If `receiverMap` exists:
- for each `topicBegin` in `receiverMap`
- for each `command` in `commandMap`
- subscribe to `topicBegin + commandMap[command] + "/+"`

If `receiverMap` is falsy:
- for each `command` in `commandMap`
- subscribe to `commandMap[command] + "/+"`

## 7.3 System subscriptions

Always add:
- `$SYS/serialdevice/#` with `options.qos`

## 8. Serial Input Parsing

Parser class: `ParseSerialData`.

Stateful buffer:
- `_receivedDataAsString` accumulates decoded bytes across calls.
- `TextDecoder` decodes incoming `Uint8Array` chunks.

## 8.1 Noise skipping

Before object parsing:
- strip all leading chars until first `[` or `{`.
- all discarded chars are ignored permanently.

## 8.2 Object extraction

If first char is `[`:
- parse up to first `]` inclusive as JSON array.

If first char is `{`:
- parse up to first `}` inclusive as JSON object.

If closing bracket not yet present:
- return `null` (wait for more data).

On JSON parse error:
- log error
- drop that object fragment
- continue with remaining stream on next call

## 8.3 Transform rules

Given decoded object `receivedObject`:

1. Array format `[sender, command, valueRaw]`
- interfaceName = `"switch"` if `command === "switch"`, else `"i2c"`
- value:
  - if switch command: convert bit string (e.g. `"0110"`) with `_bitStringToValue`
  - else use raw third element
- result = `SerialMessage(interfaceName, sender, null, command, value)`

2. YAHA Arduino object format `{ S, R, K, V, ... }`
- identified by presence of key `S`
- result = `SerialMessage("serial", S, R, K, V)`

3. FS20 object format `{ Hauscode, Adresse, Befehl, ... }`
- identified by presence of key `Hauscode`
- command = `Hauscode + "/" + Adresse`
- value = `"off"` if `Befehl === 0`, else `"on"`
- action = `"/set"`
- result = `SerialMessage("fs20", null, null, command, value, "/set")`

4. Unknown object type:
- return `null`

## 8.4 Bit string conversion exact behavior

`_bitStringToValue(bitString)` loops from right to left:
- `result *= 2`
- increment if char is `'1'`

This effectively treats rightmost character as least significant bit in current implementation order.

## 9. Serial -> MQTT Mapping

Mapper class: `SerialToMQTT`.

Public function:
- `toMqttMessages(serialMessage) -> Message[]`

## 9.1 General flow

If `serialMessage.interfaceName === "switch"`:
- use switch expansion logic returning zero or more messages.

Else:
- resolve topic via sender+command+action mapping
- resolve value via inverse valueMap mapping where available
- emit one MQTT `Message(topic, value, "received from arduino")`

## 9.2 Topic construction for non-switch

`topic = topicBegin + topicEnd + serialMessage.action`

- `topicBegin` from receiverMap lookup by sender address.
- `topicEnd` from commandMap, fallback sendMap.
- `action` appended verbatim.

### 9.2.1 sender -> topicBegin

For each `topicBegin` in `receiverMap`:
- match if `serialAddress === address` OR `serialAddress === address.toString()`

If no match and `serialAddress !== null`:
- throw `Error("Unknown serial address: " + serialAddress)`

If `serialAddress === null` and no match:
- return empty prefix.

### 9.2.2 command -> topicEnd

Lookup order:
1. `commandMap[serialCommand]`
2. if not found and `sendMap` exists: `sendMap[serialCommand]`

If still undefined:
- throw `Error("Unknown serial command: " + serialCommand)`

## 9.3 Value reverse-mapping

`_toMqttValue(interfaceDefinition, command, serialValue)`:
- default result = `serialValue`
- if `valueMap` exists:
  - iterate all map blocks
  - if `usedby` contains `command`
  - find `interfaceValue` with exact mapped integer equality
  - result becomes matching key string

No coercion or fallback conversion besides this lookup.

## 9.4 Switch expansion logic

Inputs:
- `command`, `sender`, `value`
- for each configured topic in `topicMap`

Condition to consider entry:
- incoming `command === required.command`
- incoming `sender === required.address` (strict equality)

Then:
- `isSwitchOnMessage = (value & SWITCH_ON) !== 0`
- `isSwitchOffMessage = (value & SWITCH_OFF) !== 0`
- `isSwitchMessage = isSwitchOnMessage || isSwitchOffMessage`
- `bitIsSet = (value & required.value) !== 0`

Publish cases:
1. Not switch command frame (`!isSwitchMessage`):
- publish `on` if `bitIsSet`, else `off`

2. Switch command frame and target bit set:
- publish `off` if `isSwitchOffMessage`
- else publish `on`

3. Switch command frame and target bit not set:
- publish nothing

All switch publishes use reason:
- `"received from arduino"`

## 10. MQTT -> Serial Mapping

Mapper class: `MQTTToSerial` (`MQTTMessageToSerialMessage`).

Public function:
- `toSerialMessage(mqttMessage) -> SerialMessage`

## 10.1 Decision path

1. Try direct `topicMap` route (`_getSerialMessageByTopic`).
2. If not matched, resolve by suffix command map (`_getInterfaceCommandByTopic`) and receiver prefix lookup.

## 10.2 topicMap route (switch-oriented)

For each interface:
- if `topicMap` object exists and contains exact key `mqttMessage.topic`
- create `SerialMessage(interfaceName, null, settings.address, settings.command, settings.value)`
- modify value:
  - if mqtt value is `"on"` or `"1"`: add `SWITCH_ON`
  - else add `SWITCH_OFF`

No case-insensitive compare in this path.

## 10.3 command suffix route

### 10.3.1 Resolve interface+command

Iterate all interfaces and each `commandMap` entry:
- match if `topic.toLowerCase().endsWith(commandMap[command].toLowerCase())`
- first match wins (iteration-order dependent)

If no match:
- throw `Error("undefined device setting " + topic)`

### 10.3.2 Resolve receiver

From target interface `receiverMap`:
- find first prefix where `topic.toLowerCase().startsWith(prefix.toLowerCase())`
- return mapped receiver value
- if no match or no receiverMap: return empty string `""`

### 10.3.3 Resolve value

`_getSerialValueByCommand(serialMessage, mqttValue)`:
- start with `result = mqttValue`
- if numeric (`!isNaN(result)`): cast `Number(result)`
- if string:
  - apply `_mapValue(valueMap, command, mqttValue)`
  - then map literal `"on" -> 1`, `"off" -> 0`
- require integer type after mapping
- enforce range check currently implemented as:
  - if `mqttValue < 0 || mqttValue > 0xFFFF` throw

Important compatibility note:
- Range check compares original `mqttValue` variable, not normalized integer `result`.
- Reconstruction must preserve this behavior for strict parity.

`_mapValue` behavior:
- if `valueMap` exists:
  - find first map block where `usedby` includes command
  - try `valueMapItem.map[value.toLowerCase()]`
  - if integer -> use it
- else fallback bool-like mapping:
  - `on|true|1 -> 1`
  - `off|false|0 -> 0`

If mapping still non-integer:
- throw `Error("The provided value is not an integer: " + mqttValue)`

## 10.4 SerialMessage produced by suffix route

Constructed fields:
- `sender = null`
- `interfaceName = resolved interface`
- `command = resolved command key`
- `receiver = resolved prefix-mapped receiver or ""`
- `value = normalized integer`

## 11. Serial Payload Serialization

Function: `serialMessageToString(message)`.

Per interface:

1. `switch`
- `switchMessageToString(message)`

2. `i2c`
- `"C" + receiver + command + value`

3. `serial`
- `JSON.stringify({ S: sender, R: receiver, C: command, V: value })`

4. `fs20`
- command split by `/`
- `"G" + commandArray[1] + value`

Undefined interface:
- function returns `undefined`.

## 11.1 switchMessageToString

- compute least significant set bit index with `lsb(value)`
- if index `<= 8`:
  - prefix `"s" + index`
  - append:
    - `"H"` if `value & SWITCH_ON`
    - `"L"` if `value & SWITCH_OFF`
    - else empty string
- if index invalid or no ON/OFF bit:
  - return empty string

`lsb` returns:
- index of least significant bit for values >=1 with set bit
- `-1` if no bit set

## 12. Service Runtime Behavior

Class: `SerialDevice`.

## 12.1 Constructor side effects

Initializes:
- sanitized options
- publish callback registry
- match message tracker
- serial connection adapter
- task queue with 100ms spacing
- mappers/parser
- `_close = false`

Registers queue handler:
- on each task payload, call `_sendDataToSerial(payload)` async.

## 12.2 Event API

`on(event, callback)`
- Delegates to `Callbacks` helper.
- Supported event list is fixed to `publish`.

## 12.3 run()

1. register serial `data` callback to `_processReceivedData`.
2. call `_openSerialInterface()`.
3. on success log `"Serial service is running"`.
4. on open failure:
- log error
- call `close()`
5. start keep-alive loop `_runSendKeepAlive()` (not awaited).

## 12.4 close()

- set `_close = true`
- try serial close, log errors if any
- log `"serial device service closed"`

## 12.5 Incoming serial processing

`_processReceivedData(serialData)`:
- parse to `SerialMessage|null`
- trace input
- if message exists:
  - convert to MQTT messages
  - publish each
- all errors caught and logged (no throw escape)

## 12.6 Outgoing MQTT handling

`handleMessage(message)`:
1. copy incoming to new `Message(topic, value, reason)`.
2. trace incoming.
3. if topic exactly `$SYS/serialdevice/trace/set`:
- set `this._options.trace = mqttMessage.value`

4. else normal route:
- append reason: `"received by serialDevice interface service"`
- add message to `MatchMessages`
- strip trailing `/set` from topic
- map to `SerialMessage`
- serialize to wire string
- enqueue on task queue

All exceptions are caught and logged.

## 12.7 Publish path and reply matching

`_publish(mqttMessages)` loops per message:
1. `isSetMessage = topic.endsWith('/set')`
2. remove trailing `/set` from topic
3. if matching request exists:
- update using `matchAndUpdateReplyMessage`
4. else if original was set message:
- append `/set` back
5. set `message.qos = options.qos`
6. invoke publish callback

Effect:
- reply correlation can suppress/normalize set suffix behavior.

## 12.8 Keep alive

Loop while not closed:
- send serial string `"at"`
- swallow/log errors
- delay `keepAliveDelayInSeconds * 1000`

## 12.9 Serial send with retry

`_sendDataToSerial(serialString)` uses retry counter initialized to 3.

Loop while `retry > 0`:
- try send; on success set retry to 0.
- on error:
  - log error
  - conditional `if (retry === 0) throw err` (effectively unreachable in catch path)
  - call `_openSerialInterface()`
  - decrement retry

Compatibility note:
- Due to loop condition and decrement position, this yields up to three attempts with reopen between failures.
- The `if (retry === 0)` branch is dead in current control flow; preserve behavior.

## 12.10 Serial open with retry

`_openSerialInterface()`:
- constants: RETRY=10, DELAY=15s
- if already open:
  - close serial
  - mark closed
- while not open:
  - try open(port, baudrate)
  - on failure:
    - log error
    - list available ports
    - log retry countdown info
    - increment loop counter
    - if loop >= RETRY -> throw
    - delay 15s

## 13. Trace Behavior

Option `trace` values:
- `errors`: only error logs from error handling paths
- `messages`: message-level logs
- `internal`: message-level + raw serial/raw send logs

Trace outputs:
- incoming raw serial data (internal only)
- parsed serial message (messages/internal)
- incoming mqtt message (messages/internal)
- outgoing serial string (internal only)

Time format:
- uses `new Date().toLocaleTimeString()` in logs.

## 14. Error Handling and Failure Semantics

Global pattern:
- Operational methods catch exceptions and pass to error logger.
- Service rarely throws to caller after startup.

Throwing points in mapper/parser path:
- unknown interface name
- unknown serial address (when sender not null)
- unknown serial command
- undefined MQTT device setting
- invalid non-integer value
- invalid value range

These exceptions are usually caught by caller (`handleMessage` or `_processReceivedData`) and logged, not propagated.

Startup/open exceptions:
- `_openSerialInterface` may throw after retry exhaustion.
- `run()` catches and triggers `close()`.

## 15. Exact Compatibility Rules (Must Preserve)

1. Topic matching is case-insensitive for suffix/prefix matching in `_getInterfaceCommandByTopic` and `_getReceiverByTopic`.
2. Direct `topicMap` matching is exact and case-sensitive.
3. Receiver lookup returns empty string when no prefix matches (not null).
4. Switch route in serial->mqtt may emit multiple MQTT messages for one serial frame.
5. `fs20` incoming objects emit topic suffix `/set` through `SerialMessage.action`.
6. Keep-alive payload must be exactly `at`.
7. Outgoing sends are task-queued with 100ms spacing.
8. Publish path removes `/set` before matching and may add it back.
9. `serial` wire output JSON uses key `C` (not `K`) for command.
10. `_getSerialValueByCommand` range check compares `mqttValue` variable, not normalized `result`.
11. Unknown parse object types return null silently.
12. Noise bytes before `[` or `{` are dropped.

## 16. Integration Contract for New YAHA Client

A parity-compatible new YAHA client must expose equivalent external behavior:

1. Input contracts
- MQTT inbound messages with topic+value.
- Serial inbound byte stream chunks.

2. Output contracts
- MQTT publish callbacks with mapped topic/value/reason/qos.
- Serial outbound strings exactly as specified.

3. Control contracts
- Runtime start/stop.
- Configurable trace level over MQTT system topic.
- Deterministic subscribe derivation.

4. Timing contracts
- Outgoing serial rate limiting (100ms task spacing).
- Keep-alive interval by configuration.

5. Fault tolerance contracts
- Serial open retry strategy.
- Send retry with reopen attempts.
- Non-fatal conversion and parse failures logged and ignored.

## 17. Parity Test Matrix for Reimplementation

Minimum parity verification matrix:

1. Parser tests
- chunked objects across multiple frames
- mixed noise + valid payload
- array format i2c and switch
- YAHA object format
- FS20 format with `/set` action
- invalid JSON fragment handling

2. MQTT->Serial tests
- direct switch `topicMap` on/off conversion
- suffix command resolution by interface commandMap
- receiver prefix resolution
- valueMap mapping and fallback on/off coercion
- errors for unknown topic and invalid values

3. Serial->MQTT tests
- sender/command topic derivation
- commandMap and sendMap fallback
- value reverse mapping
- switch bit expansion logic
- unknown address/command failures

4. Serialization tests
- switch to `s<bit><H|L>`
- i2c format `C<receiver><command><value>`
- serial JSON `{S,R,C,V}`
- fs20 `G<address><value>` extraction from command segment

5. Service tests
- subscribe map generation
- trace topic runtime update
- `/set` normalization and reply matching behavior
- keep alive loop sends `at`
- send queue spacing and retry/open behavior

## 18. Reconstruction Guidance (Implementation Notes)

For strict 1:1 parity in a new YAHA client:
- Preserve mapping order/iteration order dependencies.
- Preserve permissive/quirky behaviors listed above.
- Preserve all topic string concatenation rules exactly.
- Preserve message reasons:
  - incoming serial -> MQTT reason: `"received from arduino"`
  - incoming MQTT handled by service adds reason: `"received by serialDevice interface service"`
- Preserve system trace control topic exactly:
  - `$SYS/serialdevice/trace/set`

If modernization is desired later, do it behind explicit compatibility mode flags and maintain this mode as default for migration safety.

## 19. Change History Relevance

From `CHANGELOG.md`, behavior-significant entries for parity:
- 1.4.1: corrected trace topic usage.
- 1.4.0: ensured 100ms delay between serial sends.
- 1.2.0: fs20 separation of `commandMap` and `sendMap`.

These are part of the baseline behavior and must be included in the reconstructed client.
