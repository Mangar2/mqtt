# serial_device_client

Phase 5 scope in this module:
- define standalone runtime config types for SerialDevice client
- parse and validate SerialDevice INI sections into typed domain config
- preserve deterministic fallback logging for invalid recoverable values
- compose standalone runtime objects (SerialDevice component + MQTT client runtime + serial transport)

## Public API

### Struct SerialDeviceClientRuntimeConfig

Fields:
- serialDeviceConfig (`SerialDeviceConfig`)
- mqttConfig (`YahaMqttClient::Config`)

### Function tryLoadSerialDeviceConfigFromIni

Signature:
- `bool tryLoadSerialDeviceConfigFromIni(const IniDocument&, SerialDeviceConfig&, std::string&)`

Behavior:
- required key: `[serialdevice] serialPortName`
- optional keys with default fallback:
  - baudrate in [1..4000000]
  - qos in [0..2]
  - trace in {errors,messages,internal}
  - logIncomingMessages bool
  - logOutgoingMessages bool
  - logReason bool
  - keepAliveDelayInSeconds in [1..86400]
- parses legacy interface sections:
  - `[serialdevice.i2c.commandMap]`
  - `[serialdevice.i2c.receiverMap]`
  - `[serialdevice.fs20.commandMap]`
  - `[serialdevice.fs20.sendMap]`
  - `[serialdevice.switch.topicMap]` with `command,value,address`
  - `[serialdevice.serial.commandMap]`
  - `[serialdevice.serial.receiverMap]`
  - `[serialdevice.serial.valueMap]` with `usedby=...;map=...;description=...`
- deterministic warning logs to `std::cerr` for invalid recoverable values

### Function tryLoadSerialDeviceClientRuntimeConfigFromIni

Signature:
- `bool tryLoadSerialDeviceClientRuntimeConfigFromIni(const IniDocument&, SerialDeviceClientRuntimeConfig&, std::string&)`

Behavior:
- maps domain config via `tryLoadSerialDeviceConfigFromIni`
- maps MQTT config via `tryLoadMqttClientConfigFromIni`
- keeps MQTT defaults on recoverable MQTT parse errors and logs warning
- maps shared message-log booleans from `[serialdevice]` via `tryLoadMessageLogConfigFromIni`
- propagates `[serialdevice].logReason` into `mqttConfig.logReason` for reason-chain output consistency between transport trace and component logs

### Struct SerialDeviceClientRuntimeObjects

Fields:
- runtimeConfig (`SerialDeviceClientRuntimeConfig`)
- component (`std::unique_ptr<SerialDeviceComponent>`)
- serialTransport (`std::shared_ptr<ISerialDeviceTransport>`)
- mqttClient (`std::unique_ptr<YahaMqttClient>`)
- runtime (`std::unique_ptr<YahaMqttClientRuntime>`)

### Function buildSerialDeviceClientRuntime

Signature:
- `SerialDeviceClientRuntimeObjects buildSerialDeviceClientRuntime(const SerialDeviceClientRuntimeConfig&)`

Behavior:
- creates serial transport implementation for SerialDevice runtime using existing byte-serial adapter
- creates `SerialDeviceComponent` with parsed `serialDeviceConfig`
- creates `YahaMqttClient` with broker transport wiring
- creates `YahaMqttClientRuntime` for signal-driven process lifecycle
- returns fully wired runtime object bundle for standalone main

## Files

- serial_device_client_config.h
- serial_device_client_config.cpp
- serial_device_client_app.h
- serial_device_client_app.cpp
- test/TEST_SPEC.md
- test/serial_device_client_config_test.cpp
- test/serial_device_client_app_test.cpp
