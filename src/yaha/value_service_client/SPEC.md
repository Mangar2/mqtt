# value_service_client — YAHA ValueService Runtime Types and Domain Mapping

## Purpose

Defines ValueService client runtime config data types and ValueService-specific
INI mapping used by standalone composition.

## Public API

### Struct `ValueServiceClientRuntimeConfig`

| Field | Type | Notes |
|------|------|-------|
| `valueServiceConfig` | `ValueServiceConfig` | ValueService domain settings |
| `mqttConfig` | `YahaMqttClient::Config` | Generic MQTT runtime settings |

### Config mapping helpers

| Function | Signature | Notes |
|---------|-----------|-------|
| `tryLoadValueServiceConfigFromIni` | `(const IniDocument&, ValueServiceConfig&, std::string&) -> bool` | Maps ValueService + FileStore fields |
| `tryLoadValueServiceClientRuntimeConfigFromIni` | `(const IniDocument&, ValueServiceClientRuntimeConfig&, std::string&) -> bool` | Maps full runtime config |

## Runtime composition behavior

`src/yaha_valueserviceclient_main.cpp` composes runtime directly:

- start order: `ValueServiceComponent::run()` then `YahaMqttClient::run()` via `YahaMqttClientRuntime`
- stop order: `YahaMqttClient::close()` then `ValueServiceComponent::close()` via `YahaMqttClientRuntime`

Standalone main behavior:

- parse CLI args (`[config-path]`, `--trace-messages`, `--help`)
- load INI config with `IniDocument`
- map full runtime config with `tryLoadValueServiceClientRuntimeConfigFromIni`
- construct `ValueServiceComponent`
- construct `YahaMqttClient` with `makeBrokerTransport()`
- run until signal using `YahaMqttClientRuntime`

## Configuration format

Supported INI sections:

- `[mqtt]`
  - `host`, `port`, `clientId`, `reconnectDelayMs`, `keepAliveIntervalMs`, `loopSleepMs`
- `[filestore]`
  - `host`, `port`, `filename`, `use`, `topicPrefix`
- `[valueservice]`
  - `subscribeQoS`, `valuesFileName`

Semantics:
- `filestore.filename` is the single source for ValueService value-map key name.
- `filestore.topicPrefix` is the single source for FileStore monitoring event subscription prefix.

Validation rules:
- `filestore.port` must be `1..65535`.
- `valueservice.subscribeQoS` must be `0..2`.
- `filestore.use` must be valid boolean token.

## Files

| File | Role |
|------|------|
| `value_service_client_app.h` | Runtime config declarations |
| `value_service_client_app.cpp` | Runtime config mapping implementation |

Related verification artifacts:

- Runtime config mapping tests are implemented in
  `src/yaha/value_service/test/value_service_client_config_test.cpp` with
  test cases listed in `src/yaha/value_service/test/TEST_SPEC.md`.
