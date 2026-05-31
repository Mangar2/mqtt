# serial_device

Phase 4 scope in this module:
- define SerialDevice domain configuration contract
- derive MQTT subscriptions from configured interface maps
- preserve legacy subscription behavior quirks from `@mangar2/serialdevice`
- provide internal serial message model with legacy-compatible scalar types
- parse chunked serial stream input to normalized serial messages
- serialize internal serial messages to exact wire payload strings
- map MQTT topic/value input to serial messages with topicMap and suffix routes
- map serial messages to MQTT publish messages including switch expansion and value-map reverse conversion
- implement IMqttComponent runtime lifecycle with serial open/close and receive processing
- implement keep-alive loop payload `at` with configurable delay
- implement send queue pacing (100ms) and retry+reopen behavior for serial sends
- implement trace-topic runtime control `$SYS/serialdevice/trace/set`

## Public types

### Struct SerialDeviceSwitchTopicMapping

Fields:
- command (string)
- value (unsigned 0..65535)
- address (string)

### Struct SerialDeviceValueMapDefinition

Fields:
- description (string)
- usedBy (array of command strings)
- map (string -> unsigned 0..65535)

### Struct SerialDeviceInterfaceDefinition

Fields:
- commandMap (serial command -> topic suffix)
- sendMap (serial command -> topic suffix)
- receiverMap (topic prefix -> address token)
- receiverMapProvided (bool)
- topicMap (topic -> switch mapping)
- valueMap (value-map-name -> value-map-definition)

Legacy compatibility behavior:
- `receiverMapProvided=true` and empty `receiverMap` means no derived command subscriptions.
- `receiverMapProvided=false` means derive command subscriptions without topic prefix.

### Struct SerialDeviceConfig

Fields:
- serialPortName (required, non-empty)
- baudrate (default 38400)
- subscribeQos in {0,1,2} (default QoS 1)
- traceLevel in {errors,messages,internal} (default internal)
- logIncomingMessages (default false)
- logOutgoingMessages (default false)
- logReason (default true)
- keepAliveDelayInSeconds (default 30)
- interfaces (map with legacy interface names: i2c, fs20, switch, serial)

### Type SerialDeviceEndpoint

`std::variant<std::monostate, std::int64_t, std::string>`

Semantics:
- represents sender/receiver fields with legacy-compatible `null`, numeric, and string values.

### Type SerialDeviceValue

`std::variant<std::int64_t, std::string>`

Semantics:
- represents serial payload values as number or text (`on`/`off`, fs20 values, and mapped values).

### Struct SerialDeviceMessage

Fields:
- interfaceName (string)
- sender (`SerialDeviceEndpoint`)
- receiver (`SerialDeviceEndpoint`)
- command (string)
- value (`SerialDeviceValue`)
- action (string, default empty)

Behavior:
- `toString()` yields legacy-like debug format:
  - `Interface: <interface> Sender: <sender> Receiver: <receiver> Command: <command> Value: <value>`

## Functions

### deriveSerialDeviceSubscriptions

Signature:
- `SubscriptionMap deriveSerialDeviceSubscriptions(const SerialDeviceConfig& config)`

Behavior:
- For every `topicMap` topic: add `<topic>/+` with configured QoS.
- For `commandMap` subscriptions:
  - if `receiverMapProvided=true`: for each receiver prefix and command map entry add `<prefix><topicSuffix>/+`
  - if `receiverMapProvided=false`: for each command map entry add `<topicSuffix>/+`
- Always add `$SYS/serialdevice/#` with configured QoS.

### `SerialDeviceStreamParser::parseChunk`

Signature:
- `std::optional<SerialDeviceMessage> parseChunk(std::string_view chunkText)`

Behavior:
- maintains internal receive buffer across calls.
- drops leading noise until first `[` or `{`.
- extracts first complete `[...]` or `{...}` frame.
- on JSON parse failure: drops malformed frame and continues with remaining stream data.
- transforms known payload families:
  - array `[sender, command, value]` -> `i2c` or `switch` message (`switch` bit-string conversion parity preserved)
  - object `{S,R,K,V}` -> `serial` message
  - object `{Hauscode,Adresse,Befehl}` -> `fs20` message with `action="/set"` and `off/on` mapping
- unknown payload objects return no message (ignored).

### serialDeviceMessageToWireString

Signature:
- `std::string serialDeviceMessageToWireString(const SerialDeviceMessage& messageValue)`

Behavior:
- `switch` -> `s<lsb><H|L>` with `SWITCH_ON=0x4000`, `SWITCH_OFF=0x2000`
- `i2c` -> `C<receiver><command><value>`
- `serial` -> JSON object string with keys `S`, `R`, `C`, `V`
- `fs20` -> `G<commandPartAfterSlash><value>`
- unsupported/invalid payload returns empty string.

### mapMqttToSerialMessage

Signature:
- `SerialDeviceMessage mapMqttToSerialMessage(const SerialDeviceConfig&, const std::string&, const std::string&)`

Behavior:
- direct topicMap route (exact topic match) has priority.
- fallback command suffix route uses case-insensitive topic-end matching against commandMap.
- receiver resolution uses case-insensitive topic-prefix matching against receiverMap; no match returns empty string endpoint.
- value conversion supports numeric text, valueMap conversion, and on/off-style fallback mapping.
- error paths include:
  - `undefined device setting <topic>` for unknown mapping
  - `The provided value is not an integer: <value>` for invalid value normalization

Legacy compatibility notes:
- direct topicMap route remains case-sensitive.
- topic suffix and receiver prefix route remain case-insensitive.
- switch ON/OFF bit augmentation uses `SWITCH_ON=0x4000`, `SWITCH_OFF=0x2000`.

### mapSerialMessageToMqttMessages

Signature:
- `std::vector<Message> mapSerialMessageToMqttMessages(const SerialDeviceConfig&, const SerialDeviceMessage&)`

Behavior:
- non-switch interfaces map to exactly one MQTT message:
  - topic = `<topicPrefix><topicSuffix><action>`
  - value uses reverse valueMap lookup where configured
  - reason text = `received from arduino`
  - qos = 0
- switch interface can map to zero, one, or multiple messages based on switch flag and bit-state rules.
- topic suffix lookup uses commandMap and sendMap fallback.
- unknown serial sender address and unknown serial command throw runtime errors.

### Interface ISerialDeviceTransport

Purpose:
- abstract runtime serial port boundary for open/close/send/list and receive callback wiring.

Operations:
- `open(portName, baudrate)`
- `close()`
- `sendData(payloadText)`
- `listAvailablePorts()`
- `setReceiveCallback(callback)`
- `isOpen()`

### Class SerialDeviceComponent

Type:
- `IMqttComponent` implementation for phase-4 runtime behavior.

Behavior:
- `run()` wires receive callback, opens serial interface, and starts:
  - send-queue worker (100ms spacing)
  - keep-alive worker (`at` payload)
- `close()` stops workers and closes serial transport.
- `handleMessage()`
  - special control topic `$SYS/serialdevice/trace/set` updates runtime trace level
  - normal messages are normalized (`/set` stripped), mapped, serialized, and queued for sending
- receive path parses serial frames, maps to MQTT messages, and publishes via callback with configured QoS.
- message-flow logging uses shared YAHA message-log service format (`component="serial_device"`) and is controlled by `logIncomingMessages` / `logOutgoingMessages` / `logReason`.
- publish callback failures emit structured `event=publish_failed` logs with category/reason metadata.

Legacy compatibility notes:
- send retry loop keeps the legacy unreachable `retry==0` throw branch shape.
- serial open retry uses fixed retry count and delay values consistent with reconstruction spec.
- publish path preserves legacy `/set` re-add behavior for non-matching replies.
- reply matching keeps legacy topic/value correlation behavior and merges request reason-chain context into matched reply publishes.

## Files

- serial_device_contract.h
- serial_device_contract.cpp
- serial_device_message.h
- serial_device_message.cpp
- serial_device_parser.h
- serial_device_parser.cpp
- serial_device_wire_serializer.h
- serial_device_wire_serializer.cpp
- serial_device_mqtt_to_serial_mapper.h
- serial_device_mqtt_to_serial_mapper.cpp
- serial_device_serial_to_mqtt_mapper.h
- serial_device_serial_to_mqtt_mapper.cpp
- serial_device_component.h
- serial_device_component.cpp
- test/TEST_SPEC.md
- test/serial_device_contract_test.cpp
- test/serial_device_oracle_parity_test.cpp
- test/serial_device_parser_oracle_test.cpp
- test/serial_device_phase2_unit_test.cpp
- test/serial_device_phase3_unit_test.cpp
- test/serial_device_wire_oracle_test.cpp
- test/serial_device_mapping_oracle_test.cpp
- test/serial_device_runtime_oracle_test.cpp
