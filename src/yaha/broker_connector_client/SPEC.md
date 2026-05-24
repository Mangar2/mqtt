# broker_connector_client — Broker Connector Standalone Composition

## Purpose

Provides Phase 4 composition/config layer for the YAHA Broker Connector standalone executable.

This module is responsible for:
- loading and mapping INI config into connector runtime config
- mapping receiver broker settings to generic `YahaMqttClient::Config`
- keeping main composition thin while using generic `YahaMqttClientRuntime`

Protocol and domain logic remain in `broker_connector/`. Runtime orchestration is handled by generic `mqtt_client_runtime`.

## Public API

### Struct `BrokerConnectorClientRuntimeConfig`

| Field | Type | Meaning |
|------|------|---------|
| `sourceConfig` | `SourceHttpBrokerConfig` | Source HTTP broker endpoint and subscription config |
| `sourceLifecycleConfig` | `SourceLifecycleConfig` | Source reconnect/ping loop timings |
| `receiverConfig` | `YahaMqttClient::Config` | Receiver MQTT client runtime config |
| `relayPolicyConfig` | `RelayPolicyConfig` | Relay retry and qos/retain mapping policy |

### Config loading functions

| Function | Signature | Notes |
|---------|-----------|-------|
| `tryLoadSourceHttpBrokerConfigFromIni` | `SourceHttpBrokerConfigLoadResult(const IniDocument&)` | Reads `sourceHttpBroker` and repeated `subscription` sections |
| `tryLoadReceiverMqttBrokerConfigFromIni` | `ReceiverMqttBrokerConfigLoadResult(const IniDocument&)` | Reads `receiverMqttBroker` section |
| `tryLoadBrokerConnectorClientRuntimeConfigFromIni` | `BrokerConnectorClientRuntimeConfigLoadResult(const IniDocument&)` | Reads source, receiver, `automation`, and `monitoring` sections |

## Configuration model

### Section `[sourceHttpBroker]`

- `host` string
- `port` uint16 in range `1..65535`
- `clientId` string
- `clean` bool (`true/false/1/0/yes/no/on/off`)
- `keepAliveSeconds` uint32 in range `1..86400`
- `listenerHost` string (callback host advertised to source broker)
- `listenerBindHost` string (local bind host for callback listener)
- `listenerPort` uint16 in range `0..65535`

### Repeated section `[subscription]`

- key `topic`: topic filter
- key `qos`: qos (`0`, `1`, `2`)
- section can be repeated for multiple subscriptions
- default when missing: `topic=#`, `qos=1`
- legacy `[sourceSubscriptions]` key/value mapping is not supported

### Section `[receiverMqttBroker]`

- `host` string
- `port` uint16 in range `1..65535`
- `clientId` string
- `reconnectDelayMs` uint32 in range `1..uint32_max`
- `keepAliveSeconds` uint32 in range `1..uint32_max` (mapped to milliseconds)
- `loopSleepMs` uint32 in range `1..uint32_max`
- `enableLifecycleTrace` bool

### Section `[automation]`

- `reconnectDelayMs` uint32 in range `1..uint32_max` (source lifecycle)
- `sourceLoopSleepMs` uint32 in range `1..uint32_max`
- `sourceKeepAliveIntervalMs` uint32 in range `1..uint32_max`
- `maxPublishRetries` uint32 in range `0..uint32_max`
- `publishRetryBackoffMs` uint32 in range `0..uint32_max`
- `normalizeQosToAtLeastOnce` bool
- `retainPassthrough` bool

### Section `[monitoring]`

- `sourceLifecycleTrace` bool
- `logIncomingMessage` bool (optional, default `true`, controls source publish-recv logs)
- `logOutgoingMessage` bool (optional, default `true`, controls receiver mqtt sent/recv trace logs)

Monitoring message-log booleans are mapped through shared message-log INI helper (`tryLoadMessageLogConfigFromIni`) with unchanged legacy singular key names.

## Default handling

All fields use the defaults from their target config structs when keys are missing.
Only present keys override defaults.

## Error handling

- Parsing keeps defaults for invalid recoverable values, emits deterministic
	warning logs to `std::cerr`, and continues.
- Invalid `[subscription]` entries fall back to default subscription `#` with warning.
- Monitoring message-log parse failures keep defaults with warning.
- Typed result structs still expose `config`/`errorMessage`; recoverable invalid
	values do not produce hard failure.
- Runtime start/stop and signal handling are delegated to generic `YahaMqttClientRuntime`.

## Files

| File | Role |
|------|------|
| `broker_connector_client_app.h` | Runtime config and INI mapping declarations |
| `broker_connector_client_app.cpp` | Runtime config and INI mapping implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/broker_connector_client_app_test.cpp` | Config mapping unit tests |
| `test/broker_connector_client_test.cpp` | Config validation unit tests |
