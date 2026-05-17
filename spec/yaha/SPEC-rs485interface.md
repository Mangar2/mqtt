# RS485Interface

## 1. Scope and objective

This document is a normative behavior specification for RS485Interface.

Goal:
- allow a complete reimplementation with communication behavior identical to the legacy reference implementation
- define protocol, timing, state machine, queue behavior, retries, and failure handling in implementation-level detail

Normative source baseline used for this spec:
- spec/@mangar2/rs485interface/rs485interface.js
- spec/@mangar2/rs485interface/rs485schedule.js
- spec/@mangar2/rs485interface/rs485tokenexchange.js
- spec/@mangar2/rs485interface/rs485state.js
- spec/@mangar2/rs485interface/serialmessage.js
- spec/@mangar2/rs485interface/readmessages.js
- spec/@mangar2/rs485interface/sendqueue.js
- spec/@mangar2/rs485interface/serialdns.js
- spec/@mangar2/rs485interface/actions.js
- spec/@mangar2/rs485interface/configuration.js

If this specification conflicts with a convenience implementation, this specification wins.

## 2. Runtime role and boundaries

RS485Interface is an IMqttComponent-style domain component that bridges MQTT and a token-based RS485 bus.

It consumes:
- inbound MQTT command messages
- inbound RS485 byte streams
- periodic scheduler ticks

It produces:
- outbound RS485 frames
- outbound MQTT status/state messages derived from non-token serial frames

Transport/runtime boundary rules:
- MQTT broker connection policy belongs to generic runtime, not RS485Interface domain logic.
- RS485 serial open/close/send and read callback integration belongs to the RS485 adapter side.

## 3. Configuration contract

### 3.1 Required and optional fields

Required logical fields:
- serialPortName: string
- baudrate: integer (default 57600)
- myAddress: integer in [1, 127] (default 1)
- maxVersion: enum {0, 1, 2} (default 1)
- tickDelay: integer milliseconds (default 100)
- timeOfDayDelayInSeconds: integer (default 60)
- blinkDelayInSeconds: integer (default 3)
- temporaryOnInSeconds: integer (default 1200 seconds = 20 minutes)
- qos: enum {0, 1, 2} (default 1)
- trace: enum {errors, messages, internal} (default messages)
- interfaces: object
- settings: object
- status: object
- addresses: object
- topics: optional object

Address and value constraints:
- RS485 address range: 1..127 for configured endpoint addresses and sender address.
- Serial value domain: 0..65535.

### 3.2 Legacy trace-level quirk (normative)

Legacy runtime behavior uses a singular string check trace == error in error-filter logic, while configuration enum uses plural errors.

Actual behavior contract:
- accepted config values include errors, messages, internal
- error-log branch checks for error, messages, internal
- therefore trace=errors does not satisfy the error-only equality check

Required compatibility behavior:
- Reimplementation MUST preserve this singular/plural mismatch or provide an explicit compatibility mapping layer.

## 4. MQTT subscriptions and control topics

### 4.1 Derived subscriptions

getSubscriptions() returns a map topicPattern -> qos.

Derivation:
1. Build wildcard start topics from configured addresses by replacing every non-empty path segment with + and ensuring trailing slash.
2. For each wildcard start topic and each settings suffix: subscribe to startTopic + suffix + /set.
3. For each explicit entry in topics: subscribe to topic + /+.
4. Add control namespace wildcard.

### 4.2 Control namespace compatibility

Legacy control namespace:
- $SYS/rs485Interface/#
- trace command exact topic: $SYS/rs485Interface/trace/set

YAHA migration target namespace:
- $MONITOR/rs485Interface/#

Normative requirement for parity:
- internal component logic MUST preserve exact behavior for $SYS/rs485Interface/trace/set.
- if $MONITOR namespace is used externally, aliasing/mapping must occur before or at subscription/dispatch boundary without changing internal behavior.

## 5. Serial frame protocol

### 5.1 Field layout

Common semantic fields:
- sender: 1 byte (0..127 expected while decoding)
- receiver: 1 byte (0..127 expected while decoding, 0 means broadcast)
- flags byte:
   - bit 0: reply flag (1 means reply requested)
   - bits 1..7: version
- command: 1 byte (ASCII command character)
- value: 2 bytes

Version 0 transport layout (7 bytes total):
1. sender
2. receiver
3. flags
4. command
5. value high
6. value low
7. parity (XOR over bytes 1..6)

Version 1 transport layout (9 bytes total):
1. sender
2. receiver
3. flags
4. length (must be 9)
5. command
6. value high
7. value low
8. crc low
9. crc high

### 5.2 Value interpretation

Decode rule:
- for commands h, t, s: value = high + low / 100
- otherwise: value = high * 256 + low

Encode rule:
- serializer writes integer style high/low bytes.
- floating decode commands are a decode-side compatibility behavior.

### 5.3 Integrity algorithms

Parity for v0:
- XOR over payload bytes before parity byte.

CRC16 for v1:
- CCITT start value: 0xFFFF
- polynomial: 0x1021
- process byte-by-byte with MSB-first shift loop, then mask to 16 bits.
- on encode: byte 8 low, byte 9 high.

### 5.4 Decode validation and errors

Mandatory decode checks:
- sender <= 127, receiver <= 127
- supported version only {0,1}
- sufficient buffer length for computed message length
- v0 parity match
- v1 crc16 match
- v1 length byte equals 9

Error behavior:
- decoder throws with deterministic error class/text context (address/version/parity/crc/length/insufficient-data).
- caller logs according to trace policy and continues processing stream.

### 5.5 Encode behavior

SerialMessage serialization is version-driven and normative.

Flags byte construction:
- flags = (reply ? 1 : 0) + (version << 1)

Version 0 encode (7 bytes):
1. sender
2. receiver
3. flags
4. command byte
5. value high
6. value low
7. parity XOR over bytes 1..6 (array indices 0..5)

Version 1 encode (9 bytes):
1. sender
2. receiver
3. flags
4. fixed length byte 9
5. command byte
6. value high
7. value low
8. crc low
9. crc high

Version 1 CRC input range:
- calculate over bytes 1..7 (array indices 0..6)

Unsupported encode version behavior:
- throw unsupported-version error.

## 6. Stream reader behavior

Input is a byte chunk; output is a list of read results.

Noise handling:
- skip leading bytes while byte == 0 or byte > 127.

Frame extraction loop:
1. skip noise
2. attempt decode at current index
3. on success: emit {message, hex, error=""}, advance by decoded message length
4. on failure: emit {message=null, hex=remaining-bytes-from-index, error}, advance by message.length currently held by parser object

Normative legacy quirk:
- parse failure advance count depends on parser object current length (default 9 before complete decode path), not on robust resync scanning.

## 7. Token protocol constants and vocabulary

Token command byte:
- command = '!'

Token values:
- ENABLE_SEND = 1
- REGISTRATION_INFO = 2
- REGISTRATION_REQUEST = 3

State ids:
- STATE_UNKNOWN = 0
- STATE_REBOOT = 1
- STATE_SINGLE = 2
- STATE_UNREGISTERED = 3
- STATE_REGISTERED = 4

State outputs:
- STATE_UNCHANGED = 0
- ENABLE_SEND = 1
- REGISTRATION_INFO = 2
- REGISTRATION_REQUEST = 3
- STATE_CHANGED = 4

Loop timing constants (state machine internal tick domain):
- MAX_WAIT_TIMER = 100
- TIMER_SMALL_PERIOD = 3
- TIMER_LARGE_PERIOD = 7
- TIMER_LOOP = 10
- TIMEOUT_NO_ENABLE_SEND = 40

Loop pseudo-events:
- LOOP_TIMEOUT = 10
- LOOP_START = 11
- LOOP_SHORT_BREAK = 12
- LOOP_LONG_BREAK = 13

## 8. RS485State exact behavior

### 8.1 Internal state variables

Mutable variables:
- _state
- _timer
- _lastEnableSend
- rightSibling
- leftmostSibling
- maySend
- trace
- tokenLost

Constructor initial values (mandatory):
- _state = STATE_UNKNOWN
- _timer = 0
- _lastEnableSend = 0
- rightSibling = null
- leftmostSibling = null
- maySend = false
- trace = false

Dynamic runtime field note:
- tokenLost is written during registeredShortLoopBreak computation and is part of observable runtime state/debug behavior.

Receiver selection helper:
- if rightSibling != null -> receiver = rightSibling
- else if leftmostSibling != null -> receiver = leftmostSibling
- else receiver = broadcast (0)

calculateEnableSend() helper:
- if receiver == 0 -> REGISTRATION_REQUEST
- else ENABLE_SEND

### 8.2 Tick-only update behavior

updateStateNoMessage():
1. if _timer >= 100: call updateState(LOOP_TIMEOUT)
2. else evaluate _timer % 10:
    - 0 => updateState(LOOP_START)
    - 3 => updateState(LOOP_SHORT_BREAK)
    - 7 => updateState(LOOP_LONG_BREAK)
    - otherwise no event
3. if result != STATE_CHANGED then _timer += 1

### 8.3 State transition rules

UNKNOWN:
- default maySend = false
- ENABLE_SEND:
   - notForMe=true -> set UNREGISTERED, return STATE_CHANGED
   - notForMe=false -> set REGISTERED, maySend=true, return STATE_CHANGED
- REGISTRATION_INFO: no action
- REGISTRATION_REQUEST: set UNREGISTERED, return REGISTRATION_INFO
- LOOP_START:
   - if _timer == 0 then rightSibling=null and leftmostSibling=null
- LOOP_TIMEOUT: set REBOOT, return STATE_CHANGED

REBOOT:
- default maySend = false
- ENABLE_SEND: same helper behavior as UNKNOWN
- REGISTRATION_INFO: no action
- REGISTRATION_REQUEST: set UNREGISTERED, return REGISTRATION_INFO
- LOOP_START: return calculateEnableSend()
- LOOP_TIMEOUT: set SINGLE, return STATE_CHANGED

SINGLE:
- ENABLE_SEND: same helper behavior as UNKNOWN
- REGISTRATION_INFO: maySend=false, set UNKNOWN, return STATE_CHANGED
- REGISTRATION_REQUEST: maySend=false, set UNREGISTERED, return REGISTRATION_INFO
- LOOP_START: maySend=false, return REGISTRATION_REQUEST
- LOOP_SHORT_BREAK: maySend=true
- LOOP_TIMEOUT: _timer=0 (state unchanged)

UNREGISTERED:
- default maySend=false
- ENABLE_SEND:
   - if notForMe=false: set REGISTERED and maySend=true
   - always return STATE_CHANGED
- REGISTRATION_INFO: no action
- REGISTRATION_REQUEST: return REGISTRATION_INFO
- LOOP_TIMEOUT: set UNKNOWN, return STATE_CHANGED

REGISTERED:
- ENABLE_SEND:
   - if notForMe=false: maySend=true, _timer=0
   - if notForMe=true: maySend=false, _lastEnableSend=_timer
- REGISTRATION_INFO: no action
- REGISTRATION_REQUEST: no action
- LOOP_SHORT_BREAK:
   - tokenLost = (_lastEnableSend + 40 <= _timer)
   - if _timer == 3 or tokenLost:
      - _lastEnableSend = _timer
      - maySend = false
      - if rightSibling == null and tokenLost==false -> return REGISTRATION_REQUEST
      - else return ENABLE_SEND
- LOOP_LONG_BREAK:
   - if _timer == 7 and rightSibling == null and leftmostSibling != null -> return ENABLE_SEND
- LOOP_TIMEOUT: set UNREGISTERED, return STATE_CHANGED

### 8.4 setState side effects

setState(newState):
- _timer = 0
- _state = newState
- does not clear siblings except where explicitly done elsewhere
- if trace == true, log state transition including state string, leftmostSibling and rightSibling

## 9. Token exchange behavior

### 9.1 Message acceptance

Only token command '!' triggers token state processing.

isForMe definition:
- receiver == broadcast(0) OR receiver == myAddress

### 9.2 Address chain and sibling updates

On each token message:
- if sender > myAddress:
   - rightSibling becomes sender if currently null or greater than sender
- if sender < myAddress and sender != 0:
   - leftmostSibling becomes sender if currently null or smaller than sender
- then legacy normalization step applies:
   - leftmostSibling = min(leftmostSibling, sender)

Normative legacy quirk:
- the min step with null can collapse to 0 in JS semantics and therefore can force broadcast receiver behavior.
- parity-compatible reimplementation MUST preserve resulting externally visible behavior.

Additional compatibility note:
- when leftmostSibling is null, JS numeric coercion in min(null, sender) yields 0.
- this can force broadcast-style receiver selection until leftmostSibling is later overwritten by regular sibling-update logic.

### 9.3 Emitted token message construction

When RS485State returns one of {ENABLE_SEND, REGISTRATION_INFO, REGISTRATION_REQUEST}:
- create a serial message with:
   - command = '!'
   - value = returned token value
   - sender = myAddress
   - reply = false
   - receiver = broadcast(0) unless value is ENABLE_SEND
- for ENABLE_SEND, receiver = selected next receiver via sibling logic

### 9.4 Version adaptation

Initial send version = maxVersion.

First sent ENABLE_SEND frame uses maxVersion because version adaptation is not active yet.

_mayChangeVersion becomes true only after this node sends ENABLE_SEND.

Upon receiving a token message:
- if _mayChangeVersion is true
- and message.value == ENABLE_SEND
- and message.version <= maxVersion
-> set current send version to received version.

Effect:
- all subsequent outgoing frames use oldest accepted participant version.

Internal implementation note:
- legacy token exchange maintains an AddressChain structure with sorted address insertion.
- this structure does not influence token/state decisions and is not read for send decisions.

## 10. Scheduler and send queue

### 10.1 Queue semantics

Queue add rule:
- replace an existing queued message when sender, receiver, command match
- exception: command X never replaces (X messages accumulate)

Queue head access:
- sends always use queue head
- dequeue removes head

### 10.2 Tick order and gating

Per scheduler tick:
1. processStateNoMessage and send resulting token message if present
2. if maySend is true, attempt sending queued head
3. force maySend=false after queue attempt path to prevent second send in same tick window

### 10.3 Queue send retry rule

For queue head send attempts:
- send message and increment sendRetryCount
- dequeue and reset sendRetryCount to 0 if either:
   - sendRetryCount >= 10
   - message.reply == false
- otherwise keep queued (waiting for response match or further retries)

Counter persistence rule:
- sendRetryCount is not reset per tick; it persists until this dequeue+reset path runs.

### 10.4 Response match dequeue rule

On received serial frame, compare with current queued head.

isResponseMessage is true when:
- queued.sender == received.receiver
- queued.receiver == received.sender
- queued.command == received.command
- queued.value == received.value

If true: dequeue head.

Note:
- reply flag is not part of response-match predicate.
- response-match dequeue does not reset sendRetryCount in legacy behavior.

### 10.5 Async behavior quirks

Legacy scheduler uses non-awaited async calls in tick loop for queue-send path.

Compatibility requirement:
- preserve effective ordering and externally visible send behavior under same timing conditions.

## 11. MQTT -> serial command flow

handleMessage(message):
1. if topic exactly equals $SYS/rs485Interface/trace/set:
    - update trace level only
2. else:
    - add reason text: received by RS485Interface service
    - store message in matcher (reply-correlation helper)
    - run action processor
    - for each produced action message:
       - map to serial frame via SerialDNS
       - force serialMessage.reply = true
       - enqueue via scheduler

Action processor behavior:
- /set:
   - suffix check uses endsWith('/set')
   - strip suffix and schedule one immediate send callback cycle
- /temporary:
   - suffix check uses endsWith('/temporary')
   - strip suffix, send on then off with delay value seconds
   - delay from payload if integer, else temporaryOnInSeconds default
- /blink:
   - legacy suffix check uses endsWith('blink') without leading slash
   - strip suffix, toggle based on cached current state
   - cycles = max(1, amount), toggles = 2 * cycles
   - delay = blinkDelayInSeconds

## 12. Serial -> MQTT flow

For each parsed frame:
1. feed scheduler.processReceivedMessage(frame)
2. scheduler may emit token-response send
3. if frame command is not token command '!':
    - map frame to MQTT message list via SerialDNS
    - update action state cache using mapped messages
    - for each mapped MQTT message:
       - correlate via matcher
       - set qos to configured qos
       - publish callback invoke

### 12.1 SerialMessage introspection behavior

isInternal():
- returns true if command equals token command '!'
- returns false otherwise

isResponseMessage(referenceMessage) predicate:
- referenceMessage.sender == this.receiver
- referenceMessage.receiver == this.sender
- referenceMessage.command == this.command
- referenceMessage.value == this.value
- reply flag is not part of the predicate

getLoggingInfo() format behavior:
- prefix includes local time string
- message body is: sender + ' => ' + receiver + ' (r:' + replyBit + '): ' + command + ' = ' + printableValue
- printableValue maps token numeric values to strings:
   - 1 -> enable send
   - 2 -> reg. info
   - 3 -> reg. request
- output is right-padded with spaces to minimum length 42 before hex payload append
- hex payload is serialized frame bytes for current message version and length

## 13. SerialDNS mapping rules

MQTT to serial:
- if topic exists in explicit topics map:
   - use fixed command/value/address from mapping
   - for payload on/1 add SWITCH_ON (0x4000), else add SWITCH_OFF (0x2000)
- else:
   - receiver from addresses prefix match (case-insensitive startsWith)
   - command from settings suffix match (case-insensitive endsWith)
   - value conversion:
      - numeric payloads become integer
      - string payloads may map through interfaces[...].map for matching command usage

Serial to MQTT:
- first try explicit topics reverse mapping for switch-bit logic
- fallback:
   - topic = address-topic-prefix(sender) + command-suffix(settings/status)
   - value reverse-mapped through interfaces map when possible

## 14. Time-of-day broadcast

Independent loop while service is open:
- create message:
   - sender = myAddress
   - receiver = 0
   - reply = false
   - command = 'C'
   - value = hour * 60 + minute
- enqueue into scheduler
- wait timeOfDayDelayInSeconds

Send-version rule:
- although the message object may be created with default version 1, scheduler send path overwrites message.version with current negotiated token-exchange version before serialization.

## 15. Error handling and recovery

Serial open:
- close existing interface if marked open
- retry open up to 10 attempts
- retry delay 15 seconds
- list available ports on failure
- rethrow after final failure

Serial send:
- per send call retry up to 3 attempts
- on each failure:
   - log error
   - reopen serial interface

Decode/mapping/action errors:
- caught per message path
- logged
- runtime continues

Trace output:
- errors shown depending on trace filter (including legacy quirk in section 3.2)
- message logging:
   - trace=messages logs non-internal messages
   - trace=internal logs all messages including token traffic

## 16. Mandatory parity gates

A replacement implementation is accepted only if all gates pass:
1. Byte-level frame parity for v0/v1 encode and decode errors.
2. Stream parser parity for noise skipping and failure-step advancement.
3. Full RS485State transition matrix parity for every state/input/precondition combination.
4. Long deterministic replay parity (mixed token/no-message/timeout scenarios).
5. Queue/retry/response-dequeue parity including command X replacement exception.
6. Version-change parity around ENABLE_SEND sequencing.
7. Control-topic parity for trace switching behavior.

Zero tolerance rule:
- one differing field in one step is a parity failure.

## 17. Non-negotiable compatibility notes

The following legacy details are mandatory unless an explicit compatibility mode is introduced and defaults to legacy behavior:
- singular error vs plural errors trace comparison behavior
- leftmostSibling min normalization side effects
- response matching ignores reply bit
- command X queue non-replacement
- 10-attempt queue retry cutoff for reply-requesting messages
- strict token constants and loop timing constants

No simplification is allowed that changes observable communication behavior.
