# Broker Connector Contracts

## Purpose

Defines the stable Phase 1 contract boundaries for Broker Connector before implementation.

These contracts separate domain relay logic from transport adapters.

## Contract overview

Contract set:

- SourceHttpBrokerAdapter
- ReceiverPublishPort
- BrokerConnectorComponent
- ConnectorRuntimePort

Direction:

- Source adapter pushes incoming messages into component
- Component pushes publish requests into receiver publish port
- Runtime port controls start/stop lifecycle

## SourceHttpBrokerAdapter

Role:

- owns HTTP MQTT interface 1.0 session with source broker
- manages source connect, subscribe, ping, disconnect
- emits incoming publish messages to connector component

Contract methods:

- configure(SourceHttpBrokerConfig)
- start()
- close()
- setIncomingPublishCallback(callback)

Callback contract:

- callback input: RelayEnvelope
- callback must be non-blocking from adapter perspective

Source adapter obligations:

- use interface version 1.0 only
- reconnect and re-subscribe according to automation policy
- preserve source metadata needed by relay policy (qos, retain, dup, packetid when present)

## ReceiverPublishPort

Role:

- provides publish boundary to receiver standard MQTT broker
- transport implementation uses existing generic YAHA MQTT client

Contract methods:

- start()
- close()
- publish(RelayMessage, PublishOptions)

Receiver port obligations:

- do not expose broker-specific internals to component
- report publish result as success/failure for retry policy decisions

## BrokerConnectorComponent

Role:

- central fachteil for relay decisions
- receives source relay envelope
- validates and forwards to receiver publish port
- updates relay counters

Contract methods:

- onIncomingPublish(RelayEnvelope)
- getStats()
- setReceiverPublishPort(port)
- run()
- close()

Component obligations:

- must not implement receiver transport runtime logic
- must not implement source protocol wire details
- must enforce configured retry policy on receiver publish failures

## ConnectorRuntimePort

Role:

- lifecycle orchestration boundary used by main

Contract methods:

- run()
- close()

Runtime obligations:

- start order: receiver port, source adapter, component runtime
- shutdown order: source adapter, receiver port, component

## Shared data contracts

## RelayMessage

RelayMessage is YAHA Message as defined in [SPEC-message.md](./SPEC-message.md).

No connector-specific message schema allowed.

## RelayEnvelope

RelayEnvelope fields:

- message: RelayMessage
- sourceMeta:
  - qos: 0 | 1 | 2
  - retain: boolean
  - dup: boolean
  - packetid: optional integer
  - receivedAt: timestamp

Purpose:

- keep source transport metadata available for mapping and diagnostics

## PublishOptions

PublishOptions fields:

- qos: 0 | 1 | 2
- retain: boolean
- dup: optional boolean

Determined by component mapping policy and source metadata.

## Configuration contracts

## SourceHttpBrokerConfig

- host
- port
- clientId
- clean
- keepAliveSeconds
- subscribeTopics (topic->qos)
- listenerHost
- listenerPort

## ReceiverMqttBrokerConfig

- host
- port
- clientId
- cleanSession
- keepAliveSeconds
- user (optional)
- password (optional)

## AutomationConfig

- reconnectDelayMs
- maxPublishRetries
- publishRetryBackoffMs

## MonitoringConfig

- logTopicPrefix
- statsIntervalSeconds

## Behavioral invariants

1. Every accepted incoming source publish is either:
- forwarded successfully to receiver
- or counted and logged as final failed after retry policy exhausted

2. Component never mutates source adapter state directly.

3. Component never accesses receiver broker client internals directly.

4. Adapter and receiver implementations can be replaced as long as these contracts stay stable.

## Error contract

- Adapter start/connect errors are surfaced to runtime with reason text.
- Publish failures are surfaced to component as retryable or terminal failure.
- Invalid incoming payload is surfaced as validation failure and counted.

## Phase 1 completion criteria

Phase 1 contracts are complete when:

1. Component spec exists in [SPEC-broker-connector.md](./SPEC-broker-connector.md)
2. Contract spec exists in this file
3. Main architecture boundary is unambiguous:
- receiver side uses standard YAHA MQTT client
- HTTP source side is connector fachteil plus source adapter
