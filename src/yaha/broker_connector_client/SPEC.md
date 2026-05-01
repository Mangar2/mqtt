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
| `tryLoadSourceHttpBrokerConfigFromIni` | `SourceHttpBrokerConfigLoadResult(const IniDocument&)` | Reads `sourceHttpBroker` and `subscription` sections (legacy fallback: `sourceSubscriptions`) |
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
- legacy fallback: `[sourceSubscriptions]` key/value mapping is still accepted

### Section `[receiverMqttBroker]`

- `host` string
- `port` uint16 in range `1..65535`
- `clientId` string
- `reconnectDelayMs` uint32 in range `1..600000`
- `keepAliveSeconds` uint32 in range `1..86400` (mapped to milliseconds)
- `loopSleepMs` uint32 in range `1..1000`
- `enableLifecycleTrace` bool
- `enableMessageTrace` bool

### Section `[automation]`

- `reconnectDelayMs` uint32 in range `1..600000` (source lifecycle)
- `sourceLoopSleepMs` uint32 in range `1..1000`
- `sourceKeepAliveIntervalMs` uint32 in range `1..600000`
- `maxPublishRetries` uint32 in range `0..1000`
- `publishRetryBackoffMs` uint32 in range `0..600000`
- `normalizeQosToAtLeastOnce` bool
- `retainPassthrough` bool

### Section `[monitoring]`

- `sourceLifecycleTrace` bool

## Default handling

All fields use the defaults from their target config structs when keys are missing.
Only present keys override defaults.

## Error handling

- Parsing functions return typed result structs with either `config` value or `errorMessage` on first invalid field.
- Runtime start/stop and signal handling are delegated to generic `YahaMqttClientRuntime`.

## Files

| File | Role |
|------|------|
| `broker_connector_client_app.h` | Runtime config and INI mapping declarations |
| `broker_connector_client_app.cpp` | Runtime config and INI mapping implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/broker_connector_client_app_test.cpp` | Config mapping unit tests |
| `test/broker_connector_client_test.cpp` | Config validation unit tests |
