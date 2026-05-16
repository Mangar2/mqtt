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
| `keepAliveSeconds` | `std::uint32_t` | Source keep-alive value in seconds (sent as milliseconds in `/connect` payload) |
| `listenerHost` | `std::string` | Callback host advertised to source broker |
| `listenerBindHost` | `std::string` | Local callback listener bind host |
| `listenerPort` | `std::uint16_t` | Local callback listener port (`0` allowed) |
| `logIncomingMessages` | `bool` | Enables source `publish recv` logs (including plain full reason string) |
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
| `dup` | `bool` | Effective outgoing dup flag (QoS>0 only) |

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

Token usage from `/connect` response:
- `token.send`: connector-to-source session token used by outgoing `/pingreq`
- `token.receive`: currently parsed and stored for compatibility

Adapter listener handles callbacks:
- PUT `/publish`
- PUT `/pubrel`

Inbound `/publish` trace output is emitted directly at receive-point and prints parsed `topic`, `qos`, `retain`, `dup`, and `value` fields.
When `SourceHttpBrokerConfig.logIncomingMessages` is enabled, trace output includes one plain full reason string (`reason="..."`) built from all reason entries.

Ack behavior for callback listener:
- qos 1 publish -> 204 with `packet=puback`
- qos 2 publish -> 204 with `packet=pubrec`
- pubrel -> 204 with `packet=pubcomp`
- For qos 1/2 callbacks, `packetid` must be present (non-empty after trim); otherwise `/publish` returns 400 (`bad_publish_packetid`).
- If `packetid` cannot be represented as uint16, callback processing continues with `packetId` unset, but acknowledgement still echoes the exact raw header value.
- For qos 1/2 callback acknowledgements, response `packetid` echoes the exact incoming header text to preserve legacy broker matching behavior.

Compatibility note (legacy broker deviation):
- MQTT wire protocol defines Packet Identifier as a 16-bit value.
- The legacy HTTP broker path (`version=1.0`) carries `packetid` as plain text over HTTP headers and compares it as text for acknowledgement matching.
- Because the legacy broker is implemented in JavaScript, incoming header values can exceed 16-bit (and even 32-bit) numeric ranges.
- Broker connector intentionally accepts non-empty large `packetid` header values for QoS1/2 and echoes them unchanged in acks, even when they are not parseable as uint16.

## Data model

Incoming callback payload is normalized to YAHA `Message` and `SourcePublishMeta`.
If callback payload contains a `reason` field, reason entries are mapped into `Message.reason()` and forwarded unchanged through relay + receiver publish port.
The original callback JSON body is additionally preserved in `Message.rawPayload()` and forwarded unchanged to the receiver broker publish path.

## Runtime behavior

`SourceLifecycleManager` loop:
1. If disconnected, run adapter `connectAndSubscribe`.
2. On success, keep session alive with periodic `ping`.
3. On failure, wait reconnect delay and retry.
4. On shutdown, close adapter.

`connectAndSubscribe` now validates source HTTP responses concretely before reporting success:
- `/connect`: status `200`, header `packet=connack`, valid token object with non-empty `send` and `receive`
- `/subscribe`: status `200`, header `packet=suback`, matching `packetid`, parseable `qos` array with one entry per requested topic and no reject code `128`

On successful handshake, lifecycle trace logs include concrete source broker response summaries for `/connect` and `/subscribe`.

`BrokerConnectorComponent` forwarding path:
1. Rejects forwarding when not running or no publish callback is wired.
2. Increments `received` counter for each accepted source callback.
3. Maps source metadata to outgoing `Message` fields:
	- topic mapping: legacy source topics with prefix `$SYS/` are rewritten to `status/` (`$SYS/a/b -> status/a/b`) before receiver publish
	- when topic mapping rewrites `$SYS/...` to `status/...`, forwarded `Message.rawPayload()` is rewritten so embedded `message.topic` matches the mapped MQTT topic
	- qos mapping: `0 -> 0`, `1/2 -> 1` when normalization is enabled
	- retain mapping: source retain passthrough or forced false
	- dup mapping: source `dup` is forwarded for QoS>0, forced false for QoS0
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
