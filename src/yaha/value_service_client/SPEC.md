# value_service_client — YAHA ValueService Runtime Types and Domain Mapping

## Purpose

Defines ValueService client runtime config data types and ValueService-specific
INI mapping for standalone composition.

Phase 1 scope in this module is config mapping only.

## Public API

### Struct `ValueServiceClientRuntimeConfig`

| Field | Type | Notes |
|------|------|-------|
| `valueServiceConfig` | `ValueServiceConfig` | ValueService domain settings |
| `mqttConfig` | `YahaMqttClient::Config` | Generic MQTT runtime settings |

### Config mapping helpers

| Function | Signature | Notes |
|---------|-----------|-------|
| `tryLoadValueServiceConfigFromIni` | `(const IniDocument&, ValueServiceConfig&, std::string&) -> bool` | Maps ValueService + FileStore + monitoring fields |
| `tryLoadValueServiceClientRuntimeConfigFromIni` | `(const IniDocument&, ValueServiceClientRuntimeConfig&, std::string&) -> bool` | Maps full runtime config |

## Configuration format

Supported INI sections:

- `[mqtt]`
  - `host`, `port`, `clientId`, `reconnectDelayMs`, `keepAliveIntervalMs`, `loopSleepMs`
- `[filestore]`
  - `host`, `port`, `path`, `use`
- `[monitoring]`
  - `topicPrefix`
- `[valueservice]`
  - `subscribeQoS`, `valuesKeyPath`, `monitorTopicPrefix`, `valuesFileName`

Precedence rules:
- `valueservice.valuesKeyPath` overrides `filestore.path`.
- `valueservice.monitorTopicPrefix` overrides `monitoring.topicPrefix`.

Validation rules:
- `filestore.port` must be `1..65535`.
- `valueservice.subscribeQoS` must be `0..2`.
- `filestore.use` must be valid boolean token.

## Files

| File | Role |
|------|------|
| `value_service_client_app.h` | Runtime config declarations |
| `value_service_client_app.cpp` | Runtime config mapping implementation |
