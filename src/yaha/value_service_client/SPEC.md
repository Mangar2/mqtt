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

- start order:
  - start `YahaMqttClient::run()`
  - wait until MQTT broker connection is established
  - publish retained status `starting` on `$MONITOR/valueservice/status`
  - execute FileStore startup availability gate with retry policy from `[filestore]`
  - call `ValueServiceComponent::run()`
  - publish retained status `running` on `$MONITOR/valueservice/status`
- stop order:
  - on signal or controlled self-stop, publish retained status `stopped` on `$MONITOR/valueservice/status`
  - call `ValueServiceComponent::close()`
  - call `YahaMqttClient::close()`

MQTT Last Will behavior:

- runtime sets Last Will on `$MONITOR/valueservice/status`
- Will payload is retained `terminated`
- broker publishes `terminated` on ungraceful disconnect

Standalone main behavior:

- parse CLI args (`[config-path]`, `--trace-messages`, `--help`)
- load INI config with `IniDocument`
- map full runtime config with `tryLoadValueServiceClientRuntimeConfigFromIni`
- construct `ValueServiceComponent`
- construct `YahaMqttClient` with `makeBrokerTransport()`
- run until signal using `YahaMqttClientRuntime`
- run with explicit signal loop in main and graceful status publish sequence

## Configuration format

Supported INI sections:

- `[mqtt]`
  - `host`, `port`, `clientId`, `reconnectDelayMs`, `keepAliveIntervalMs`, `loopSleepMs`
- `[filestore]`
  - `host`, `port`, `filename`, `use`, `topicPrefix`, `startupRetryCount`, `startupRetryIntervalSeconds`
- `[valueservice]`
  - `subscribeQoS`, `logIncomingMessages`, `logOutgoingMessages`, `logReason`, `valuesFileName`

Semantics:
- `filestore.filename` is the single source for ValueService value-map key name.
- `filestore.topicPrefix` is the single source for FileStore monitoring event subscription prefix.

Validation rules:
- `filestore.port` must be `1..65535`.
- `filestore.startupRetryCount` must be `0..uint32_max`.
- `filestore.startupRetryIntervalSeconds` must be `1..uint32_max`.
- `valueservice.subscribeQoS` must be `0..2`.
- `filestore.use` must be valid boolean token.

Message-log mapping rules:
- `[valueservice].logIncomingMessages` and `[valueservice].logOutgoingMessages` are parsed via shared message-log INI helper and map to `ValueServiceConfig` logging flags.
- `[valueservice].logReason` controls reason-chain inclusion for ValueService message logs.
- `[valueservice].logReason` is also propagated to `mqttConfig.logReason` so `--trace-messages` transport logs and component logs stay reason-output consistent.

INI error handling:
- Invalid recoverable values do not abort config loading.
- For invalid recoverable values, loader writes deterministic warning logs to
  `std::cerr` and keeps the current default value.
- Runtime config load does not fail when MQTT sub-loader rejects one value;
  ValueService keeps MQTT defaults and logs warning context.

## Files

| File | Role |
|------|------|
| `value_service_client_app.h` | Runtime config declarations |
| `value_service_client_app.cpp` | Runtime config mapping implementation |

Related verification artifacts:

- Runtime config mapping tests are implemented in
  `src/yaha/value_service/test/value_service_client_config_test.cpp` with
  test cases listed in `src/yaha/value_service/test/TEST_SPEC.md`.
