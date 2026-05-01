# broker_connector_client — Broker Connector Standalone Composition

## Purpose

Provides Phase 4 composition/runtime layer for the YAHA Broker Connector standalone executable.

This module is responsible for:
- loading and mapping INI config into connector runtime config
- orchestrating start/stop order for source runtime, receiver publish runtime, and relay component
- exposing a thin composition API used by main

Protocol and domain logic remain in `broker_connector/`.

## Public API

### Struct `BrokerConnectorClientRuntimeConfig`

| Field | Type | Meaning |
|------|------|---------|
| `sourceConfig` | `SourceHttpBrokerConfig` | Source HTTP broker endpoint and subscription config |
| `sourceLifecycleConfig` | `SourceLifecycleConfig` | Source reconnect/ping loop timings |
| `receiverConfig` | `ReceiverMqttBrokerConfig` | Receiver MQTT client runtime config |
| `relayPolicyConfig` | `RelayPolicyConfig` | Relay retry and qos/retain mapping policy |

### Config loading functions

| Function | Signature | Notes |
|---------|-----------|-------|
| `tryLoadSourceHttpBrokerConfigFromIni` | `bool(const IniDocument&, SourceHttpBrokerConfig&, std::string&)` | Reads `sourceHttpBroker` and `sourceSubscriptions` sections |
| `tryLoadReceiverMqttBrokerConfigFromIni` | `bool(const IniDocument&, ReceiverMqttBrokerConfig&, std::string&)` | Reads `receiverMqttBroker` section |
| `tryLoadBrokerConnectorClientRuntimeConfigFromIni` | `bool(const IniDocument&, BrokerConnectorClientRuntimeConfig&, std::string&)` | Reads source, receiver, `automation`, and `monitoring` sections |

### Runtime control contracts

| Interface | Methods | Notes |
|-----------|---------|-------|
| `SourceRuntimePort` | `run()`, `close()` | Abstracts source-side lifecycle manager |
| `ConnectorRuntimePort` | `run()`, `close()` | Abstracts relay component lifecycle |

### Class `BrokerConnectorClientRuntime`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `BrokerConnectorClientRuntime(ReceiverPublishPort&, SourceRuntimePort&, ConnectorRuntimePort&)` | Receives already-wired runtime parts |
| `start` | `bool(std::string&)` | Start order: receiver -> connector -> source |
| `close` | `void()` | Shutdown order: source -> receiver -> connector |
| `runUntilSignal` | `bool(std::string&)` | Starts runtime, waits for SIGINT/SIGTERM, then closes |
| `isRunning` | `bool() const` | Runtime started flag |

## Configuration model

### Section `[sourceHttpBroker]`

- `host` string
- `port` uint16 in range `1..65535`
- `clientId` string
- `clean` bool (`true/false/1/0/yes/no/on/off`)
- `keepAliveSeconds` uint32 in range `1..86400`
- `listenerHost` string
- `listenerPort` uint16 in range `0..65535`

### Section `[sourceSubscriptions]`

- key: topic filter
- value: qos (`0`, `1`, `2`)
- default when empty/missing: `# => qos1`

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

- Parsing functions return `false` with `errorMessage` on first invalid field.
- Runtime `start` returns `false` when receiver start fails.
- Runtime `close` is idempotent.

## Files

| File | Role |
|------|------|
| `broker_connector_client_app.h` | Runtime config and INI mapping declarations |
| `broker_connector_client_app.cpp` | Runtime config and INI mapping implementation |
| `broker_connector_runtime.h` | Runtime control port interfaces and orchestrator declarations |
| `broker_connector_runtime.cpp` | Runtime orchestrator implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/broker_connector_client_app_test.cpp` | Config mapping unit tests |
| `test/broker_connector_runtime_test.cpp` | Runtime orchestration unit tests |
