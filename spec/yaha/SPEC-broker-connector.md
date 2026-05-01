# Broker Connector

## Purpose

Broker Connector relays MQTT messages from an HTTP-interface broker source to a standard MQTT broker receiver. It allows YAHA systems to bridge from HTTP-modified MQTT transport into regular MQTT transport with one dedicated standalone program.

## Role in the system

Broker Connector sits between two broker worlds.

- Upstream source: broker that implements HTTP MQTT interface 1.0
- Downstream receiver: standard MQTT broker

Connector consumes incoming publish flows from source subscriptions and produces publish flows to receiver topics. It does not change broker transport internals. It only applies relay policy.

## Standalone program structure

Connector is a standalone executable with thin main composition.

Main composes:

- BrokerConnector domain component (relay policy)
- Source HTTP MQTT adapter (interface 1.0)
- Standard YAHA MQTT client for receiver broker
- Generic runtime orchestration

Architecture boundary:

- Receiver side transport uses existing generic YAHA MQTT client unchanged.
- HTTP source session behavior belongs to connector domain side and its source adapter.

## Subscriptions

Source-side subscriptions are configured as topic->QoS map.

Connector subscribes on source broker to all configured topics and receives matching publish messages through HTTP callback endpoints.

## Published messages

Connector publishes every accepted source message to receiver broker.

Default first-version forwarding policy:

- topic forwarded unchanged
- value forwarded unchanged
- reason chain forwarded unchanged
- retain forwarded unchanged
- qos mapped by relay policy defined in configuration/spec decision

## External interfaces

Connector uses source broker HTTP MQTT interface 1.0.

Required source-facing endpoints in connector listener:

- PUT /publish
- PUT /pubrel

Required source-side client calls from connector:

- /connect
- /subscribe
- /pingreq
- /disconnect

Receiver side uses standard MQTT client API only (no custom receiver HTTP interface).

## Data model

Core runtime objects:

- RelayMessage: YAHA Message format from [SPEC-message.md](./SPEC-message.md)
- SourceSessionState: source connection and subscription state
- RelayCounters: received, forwarded, failed

Connector does not define a second message schema. It reuses YAHA Message as end-to-end relay payload.

## Behavior

Runtime behavior:

1. Connect to source HTTP broker.
2. Subscribe configured source topics.
3. Receive source publish callbacks.
4. Validate incoming message and metadata.
5. Forward to receiver broker publish API.
6. Apply retry policy on receiver publish failure.
7. Continue relay loop while runtime is active.

Reconnect behavior:

- source side reconnects and re-subscribes after failures
- receiver side reconnect is handled by generic YAHA MQTT client runtime

Duplicate behavior:

- duplicates from source QoS flows are forwarded unchanged in first version

## Persistence

No persistence in first version.

Connector state is runtime-only. After restart, connector reconnects and re-subscribes from configuration.

## Configuration

Required groups:

- sourceHttpBroker: source host/port/client/session settings and subscribe topic map
- receiverMqttBroker: receiver host/port/client/auth/session settings
- automation: reconnect and publish retry parameters
- monitoring: log prefix and stats interval

## Error handling

- Source connect error: retry source connect sequence
- Source subscribe error: fail current source sequence and retry from connect
- Invalid source payload: reject message and log failure
- Receiver publish failure: bounded retries, then drop and log
- Unknown incoming HTTP path: return not found

Failure on one message must not stop the whole relay runtime.

## Architectural notes

- Connector is domain relay logic only. It must not fork or duplicate generic YAHA MQTT transport runtime.
- Main remains orchestration only: compose, wire, run.
- Connector uses HTTP MQTT interface 1.0 contract from [SPEC-http-mqtt-interface.md](./SPEC-http-mqtt-interface.md).
- Connector reuses IMqttComponent architecture boundary from [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md) where applicable.

## Open questions

- Final QoS mapping policy source->receiver: preserve or normalize
- Final retain mapping policy: preserve or force false
- Topic namespace policy: passthrough or prefixed
- Drop-after-retry policy versus blocking relay policy
- Need for optional dedup cache in high-duplicate environments
