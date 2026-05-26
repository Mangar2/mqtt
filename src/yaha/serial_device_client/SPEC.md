# serial_device_client

Phase 1 scope in this module:
- define standalone runtime config types for SerialDevice client
- parse and validate SerialDevice INI sections into typed domain config
- preserve deterministic fallback logging for invalid recoverable values

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

## Files

- serial_device_client_config.h
- serial_device_client_config.cpp
- test/TEST_SPEC.md
- test/serial_device_client_config_test.cpp
