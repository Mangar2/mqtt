# broker_connector — Source and Relay Core for Broker Connector

## Purpose

Implements Phase 3 core of Broker Connector:
- source-side HTTP MQTT 1.0 connectivity
- receiver-side publish port built on standard `YahaMqttClient`
- relay component with retry policy and runtime counters

This module now provides complete source-to-receiver forwarding logic through `IMqttComponent` coupling. Executable composition remains out of scope (Phase 4).

## Public API

### Struct `SourceHttpBrokerConfig`

| Field | Type | Meaning |
|------|------|---------|
| `brokerHost` | `std::string` | Source broker HTTP host |
| `brokerPort` | `std::uint16_t` | Source broker HTTP port |
| `clientId` | `std::string` | Source-side client identity |
| `clean` | `bool` | Connect clean-session flag |
| `keepAliveSeconds` | `std::uint32_t` | Source keep-alive value for connect payload |
| `listenerHost` | `std::string` | Local callback listener host |
| `listenerPort` | `std::uint16_t` | Local callback listener port (`0` allowed) |
| `subscribeTopics` | `SubscriptionMap` | Topic->QoS map for source subscribe |

### Struct `SourcePublishMeta`

| Field | Type | Meaning |
|------|------|---------|
| `qos` | `Qos` | Source message qos |
| `retain` | `bool` | Source retain flag |
| `dup` | `bool` | Source duplicate flag |
| `packetId` | `std::optional<std::uint16_t>` | Source packet id if present |

### Class `SourceHttpBrokerAdapter`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `SourceHttpBrokerAdapter(SourceHttpBrokerConfig)` | stores source config |
| dtor | `~SourceHttpBrokerAdapter()` | closes listener/session |
| `setIncomingPublishCallback` | `void(SourcePublishCallback)` | callback for source incoming publish |
| `connectAndSubscribe` | `bool(std::string&)` | startup sequence: listener + connect + subscribe |
| `ping` | `bool(std::string&)` | sends source `/pingreq` |
| `close` | `void()` | best-effort `/disconnect`, stop listener |
| `isConnected` | `bool() const` | source session state |
| `listenerPort` | `std::uint16_t() const` | effective bound callback port |

### Class `SourceLifecycleManager`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `SourceLifecycleManager(SourceHttpBrokerAdapter&, SourceLifecycleConfig)` | runtime loop wrapper |
| dtor | `~SourceLifecycleManager()` | calls close |
| `run` | `void()` | starts background lifecycle loop |
| `close` | `void()` | stops loop and closes adapter |
| `isRunning` | `bool() const` | returns lifecycle loop state |

### Struct `ReceiverPublishOptions`

| Field | Type | Meaning |
|------|------|---------|
| `qos` | `Qos` | Effective outgoing receiver qos |
| `retain` | `bool` | Effective outgoing retain flag |
| `dup` | `bool` | Source dup flag for diagnostics |

### Struct `ReceiverMqttBrokerConfig`

| Field | Type | Meaning |
|------|------|---------|
| `brokerHost` | `std::string` | Receiver MQTT broker host |
| `brokerPort` | `std::uint16_t` | Receiver MQTT broker port |
| `clientId` | `std::string` | Receiver MQTT client id |
| `reconnectDelay` | `std::chrono::milliseconds` | Reconnect delay |
| `keepAliveInterval` | `std::chrono::milliseconds` | Receiver keep-alive interval |
| `loopSleep` | `std::chrono::milliseconds` | MQTT worker loop sleep interval |
| `enableLifecycleTrace` | `bool` | Enables lifecycle trace output |
| `enableMessageTrace` | `bool` | Enables sent/recv trace output |

### Interface `ReceiverPublishPort`

| Member | Signature | Notes |
|--------|-----------|-------|
| dtor | `virtual ~ReceiverPublishPort()` | virtual cleanup |
| `start` | `bool(std::string&)` | starts receiver runtime |
| `close` | `void()` | stops receiver runtime |
| `publish` | `bool(const Message&, const ReceiverPublishOptions&, std::string&)` | publishes one mapped message |
| `isConnected` | `bool() const` | receiver connection state |

### Class `ReceiverMqttPublishPort`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `ReceiverMqttPublishPort(ReceiverMqttBrokerConfig)` | uses default broker transport factory |
| ctor | `ReceiverMqttPublishPort(ReceiverMqttBrokerConfig, YahaMqttClient::Transport)` | transport injection for tests |
| dtor | `~ReceiverMqttPublishPort()` | closes runtime |
| `start` | `bool(std::string&)` | starts internal sink component and `YahaMqttClient` |
| `close` | `void()` | closes client runtime |
| `publish` | `bool(const Message&, const ReceiverPublishOptions&, std::string&)` | forwards with effective qos/retain |
| `isConnected` | `bool() const` | reports `YahaMqttClient` connection state |

### Struct `RelayPolicyConfig`

| Field | Type | Meaning |
|------|------|---------|
| `maxPublishRetries` | `std::uint32_t` | Retry attempts after first failure |
| `publishRetryBackoff` | `std::chrono::milliseconds` | Delay between retry attempts |
| `normalizeQosToAtLeastOnce` | `bool` | Maps source qos 1/2 to outgoing qos 1 |
| `retainPassthrough` | `bool` | Keep or clear source retain flag |

### Struct `RelayCounters`

| Field | Type | Meaning |
|------|------|---------|
| `received` | `std::uint64_t` | Accepted source messages |
| `forwarded` | `std::uint64_t` | Successful forwards |
| `failed` | `std::uint64_t` | Final failures after retries |

### Class `BrokerConnectorComponent`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `BrokerConnectorComponent(RelayPolicyConfig)` | stores relay policy |
| dtor | `~BrokerConnectorComponent()` | closes component |
| `setSourceAdapter` | `void(SourceHttpBrokerAdapter&, SourceLifecycleConfig)` | wires source adapter and lifecycle loop |
| `getSubscriptions` | `SubscriptionMap() const` | returns empty map for receiver side |
| `handleMessage` | `void(const Message&)` | no-op for inbound receiver messages |
| `setPublishCallback` | `void(PublishCallback)` | receives generic mqtt publish boundary |
| `run` | `void()` | marks relay active and starts source lifecycle loop |
| `close` | `void()` | marks relay inactive and stops source lifecycle loop |
| `onIncomingPublish` | `bool(const Message&, const SourcePublishMeta&)` | applies mapping + retry publish loop |
| `getStats` | `RelayCounters() const` | returns counter snapshot |
| `isRunning` | `bool() const` | active runtime state |

## Source HTTP protocol behavior

Adapter uses interface version 1.0 and supports:
- PUT `/connect`
- PUT `/subscribe`
- PUT `/pingreq`
- PUT `/disconnect`

Adapter listener handles callbacks:
- PUT `/publish`
- PUT `/pubrel`

Ack behavior for callback listener:
- qos 1 publish -> 204 with `packet=puback`
- qos 2 publish -> 204 with `packet=pubrec`
- pubrel -> 204 with `packet=pubcomp`

## Data model

Incoming callback payload is normalized to YAHA `Message` and `SourcePublishMeta`.
Reason-chain content in callback payload is currently ignored in this phase and does not alter `Message.reason()`.

## Runtime behavior

`SourceLifecycleManager` loop:
1. If disconnected, run adapter `connectAndSubscribe`.
2. On success, keep session alive with periodic `ping`.
3. On failure, wait reconnect delay and retry.
4. On shutdown, close adapter.

`BrokerConnectorComponent` forwarding path:
1. Rejects forwarding when not running or no publish callback is wired.
2. Increments `received` counter for each accepted source callback.
3. Maps source metadata to outgoing `Message` fields:
	- qos mapping: `0 -> 0`, `1/2 -> 1` when normalization is enabled
	- retain mapping: source retain passthrough or forced false
4. Calls `PublishCallback` (generic mqtt client boundary) with bounded retries.
5. Increments `forwarded` on success or `failed` after retry budget is exhausted.

## Threading model

- `SourceHttpBrokerAdapter` runs one httplib listener thread while active.
- `SourceLifecycleManager` runs one worker thread while active.
- Adapter state is protected by mutex.

## Error handling

- All network/protocol failures return `false` with human-readable `errorMessage`.
- Lifecycle loop never throws; it retries.
- Unknown callback paths return 404.

## Files

| File | Role |
|------|------|
| `source_http_adapter.h` | Source adapter public declarations |
| `source_http_adapter.cpp` | Source adapter implementation |
| `source_lifecycle_manager.h` | Lifecycle manager declarations |
| `source_lifecycle_manager.cpp` | Lifecycle manager implementation |
| `receiver_publish_port.h` | Receiver publish boundary and MQTT-backed implementation declarations |
| `receiver_publish_port.cpp` | Receiver publish port implementation |
| `relay_component.h` | Relay component declarations and counters/policy contracts |
| `relay_component.cpp` | Relay component implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/source_http_adapter_test.cpp` | Unit tests for Phase 2 behavior |
| `test/relay_component_test.cpp` | Unit tests for receiver publish port and IMqttComponent relay behavior |
