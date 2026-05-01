# broker_connector — Source HTTP Side for Broker Connector

## Purpose

Implements Phase 2 of Broker Connector: source-side connectivity to an HTTP MQTT 1.0 broker.

This module provides:
- source HTTP adapter (`SourceHttpBrokerAdapter`) for connect/subscribe/ping/disconnect
- source lifecycle manager (`SourceLifecycleManager`) for connect-retry and re-subscribe loop

Receiver-side publish transport is intentionally out of scope for this module phase.

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
| `test/TEST_SPEC.md` | Unit test specification |
| `test/source_http_adapter_test.cpp` | Unit tests for Phase 2 behavior |
