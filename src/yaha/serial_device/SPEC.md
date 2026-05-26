# serial_device

Phase 1 scope in this module:
- define SerialDevice domain configuration contract
- derive MQTT subscriptions from configured interface maps
- preserve legacy subscription behavior quirks from `@mangar2/serialdevice`

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

## Files

- serial_device_contract.h
- serial_device_contract.cpp
- test/TEST_SPEC.md
- test/serial_device_contract_test.cpp
