# serial_device

Phase 2 scope in this module:
- define SerialDevice domain configuration contract
- derive MQTT subscriptions from configured interface maps
- preserve legacy subscription behavior quirks from `@mangar2/serialdevice`
- provide internal serial message model with legacy-compatible scalar types
- parse chunked serial stream input to normalized serial messages
- serialize internal serial messages to exact wire payload strings

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

## Files

- serial_device_contract.h
- serial_device_contract.cpp
- serial_device_message.h
- serial_device_message.cpp
- serial_device_parser.h
- serial_device_parser.cpp
- serial_device_wire_serializer.h
- serial_device_wire_serializer.cpp
- test/TEST_SPEC.md
- test/serial_device_contract_test.cpp
- test/serial_device_oracle_parity_test.cpp
- test/serial_device_parser_oracle_test.cpp
- test/serial_device_phase2_unit_test.cpp
- test/serial_device_wire_oracle_test.cpp
