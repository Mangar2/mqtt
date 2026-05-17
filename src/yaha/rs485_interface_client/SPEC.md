# rs485_interface_client

Phase 1/4 scope in this module:
- define the RS485 client runtime configuration contract
- parse and validate RS485-related INI sections
- map parsed values into typed runtime structs
- return deterministic error messages for invalid or incomplete inputs
- compose standalone runtime objects (RS485 component, serial adapter, MQTT client runtime)
- provide POSIX serial adapter for frame send/receive callback binding

## Public types

### Struct Rs485InterfaceConfig

Fields:
- serialPortName (required, non-empty)
- baudrate (default 57600)
- myAddress in [1..127] (default 1)
- maxVersion in {0,1,2} (default 1)
- tickDelayMs (default 100)
- timeOfDayDelaySeconds (default 60)
- subscribeQos in {0,1,2} (default QoS 1)
- traceLevel in {errors,messages,internal} (default messages)
- blinkDelaySeconds (default 3)
- temporaryOnSeconds (default 1200)
- interfaces (required map)
- settings (required map)
- status (required map)
- addresses (required map)
- topics (optional map)

### Struct Rs485InterfaceRuntimeConfig

Fields:
- rs485Config (Rs485InterfaceConfig)
- mqttConfig (YahaMqttClient::Config)

### Struct Rs485InterfaceClientRuntimeObjects

Fields:
- rs485Config (Rs485InterfaceConfig)
- component (IMqttComponent instance)
- serialAdapter (Rs485SerialAdapter instance)
- mqttClient (YahaMqttClient instance)
- runtime (YahaMqttClientRuntime instance)

### Class Rs485SerialAdapter

Methods:
- `open(portName, baudrate, errorMessage)` opens serial device and starts read loop
- `close()` stops read loop and closes descriptor
- `send(payload, errorMessage)` writes all payload bytes
- `setReceiveCallback(callback)` installs receive callback for read chunks
- `isOpen()` returns descriptor state

## INI mapping

### Section [rs485interface]

Keys:
- serialPortName (required)
- baudrate (optional)
- myAddress (optional)
- maxVersion (optional)
- tickDelay (optional)
- timeOfDayDelayInSeconds (optional)
- qos (optional)
- trace (optional)
- blinkDelayInSeconds (optional)
- temporaryOnInSeconds (optional)

### Section [rs485interface.settings] (required)

- key: one-character command symbol
- value: topic suffix string

### Section [rs485interface.status] (required)

- key: one-character command symbol
- value: topic suffix string

### Section [rs485interface.addresses] (required)

- key: topic prefix
- value: address in [1..127]

### Section [rs485interface.topics] (optional)

- key: topic
- value format: COMMAND,VALUE,ADDRESS
- COMMAND: one character
- VALUE: unsigned in [0..65535]
- ADDRESS: unsigned in [1..127]

### Section [rs485interface.interfaces] (required)

- key: interface name
- value format: usedby=<c1,c2,...>;map=<k1:v1|k2:v2|...>
- each usedby token must be exactly one character
- each map value must be unsigned in [0..65535]

## Error contract

All parse failures return false and set errorMessage.
Error text identifies section/key and reason in deterministic form.

Runtime composition helper:
- `tryBuildRs485InterfaceClientRuntime(...)` returns false with deterministic `errorMessage` on construction failures.

## Files

- rs485_interface_client_app.h
- rs485_interface_client_app.cpp
- rs485_serial_adapter.h
- rs485_serial_adapter.cpp
- test/rs485_interface_client_config_test.cpp
