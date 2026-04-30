# Implementation Plan: Broker Connector

This plan defines a standalone YAHA program that bridges messages from a source broker with HTTP MQTT interface (version 1.0) to a target standard MQTT broker.

Direction is one-way:

- source side: subscribe and receive messages from HTTP broker
- target side: publish all received messages to MQTT receiver broker

## Goal

Build a relay client with exactly one responsibility:

- connect to HTTP broker
- subscribe on configured topics
- forward each received publish message to target MQTT broker

No transformation pipeline in first version. Message topic and value are forwarded unchanged.

## Existing specs and modules to reuse

- [spec/yaha/SPEC-http-mqtt-interface.md](./SPEC-http-mqtt-interface.md)
- [spec/yaha/SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md)
- [spec/yaha/SPEC-message.md](./SPEC-message.md)
- [spec/yaha/SPEC-http-mqtt-bridge.md](./SPEC-http-mqtt-bridge.md) (architecture pattern: standalone executable)

Reuse intent:

- keep YAHA main thin (compose only)
- keep runtime policy in generic runtime modules
- keep connector domain logic in one component class
- use existing generic YAHA MQTT client unchanged for receiver broker connection
- model HTTP source connection as connector domain part (fachteil)

## Scope

In scope:

- one-way forwarding source -> target
- HTTP interface version 1.0 only
- subscribe and reconnect behavior for source side
- reconnect behavior for target side
- minimal observability counters and logs

Out of scope for first iteration:

- bidirectional sync
- payload remapping rules
- topic rewrite rules
- dead letter queue
- persistence of pending relay messages

## Program structure

Program consists of four parts.

1. BrokerConnector domain component (fachteil)
- owns source HTTP broker session behavior and relay decisions
- no MQTT transport policy for receiver side
- exposes incoming message stream for forwarding

2. Source HTTP interface adapter
- speaks HTTP MQTT 1.0 to source broker
- performs connect, subscribe, ping, disconnect
- receives incoming publish callbacks

3. Target publisher via standard YAHA MQTT client
- uses existing generic YahaMqttClient runtime without connector-specific forks
- connector calls publish through client API/callback boundary

4. Main
- parse config
- create connector domain component
- create source HTTP adapter
- create standard YAHA MQTT client for receiver broker
- wire source callback -> connector component -> YAHA MQTT client publish
- start runtime once

## Data flow

1. Source adapter connects to HTTP broker with clientId and callback host/port.
2. Source adapter subscribes to configured topic map.
3. Source broker sends incoming messages to connector callback endpoint (/publish, /pubrel).
4. Source adapter emits normalized Message to connector domain component.
5. Connector domain component applies relay policy and forwards Message.
6. Standard YAHA MQTT client publishes to receiver MQTT broker.

## Configuration model

Define one config document with these sections.

sourceHttpBroker
- host: string
- port: integer
- clientId: string
- clean: boolean
- keepAliveSeconds: integer
- subscribeTopics: map topic -> qos
- listenerHost: string
- listenerPort: integer (0 allowed for dynamic)

receiverMqttBroker
- host: string
- port: integer
- clientId: string
- cleanSession: boolean
- keepAliveSeconds: integer
- user: optional string
- password: optional string

automation
- reconnectDelayMs: integer
- maxPublishRetries: integer
- publishRetryBackoffMs: integer

monitoring
- logTopicPrefix: string
- statsIntervalSeconds: integer

## Reliability and behavior rules

1. At least once relay target
- if source message has qos 1 or 2 then publish to target with qos 1 by default in first version
- configurable later if strict qos mapping is needed

2. Ordering
- preserve receive order per callback thread
- no global ordering guarantee across reconnects

3. Duplicate handling
- source duplicates may occur (qos2 flows)
- first version forwards duplicates unchanged
- optional dedup cache can be added later

4. Backpressure
- if target publish fails, retry up to maxPublishRetries
- after retries, log failure and continue with next message

5. Reconnect
- source adapter reconnects and re-subscribes all topics
- target adapter reconnects independently

## Error handling policy

- source connect failure: retry loop with reconnectDelayMs
- source subscribe failure: fail startup and retry full source connect sequence
- target publish failure: bounded retry then drop message with error log
- invalid incoming payload from source: reject and log
- pubrel timeout cases: handled by source HTTP MQTT stack behavior

## Security baseline

- do not log credentials
- TLS handling delegated to deployed transport layer or mqtt client capability
- optional allowlist of source host if connector exposes callback listener publicly

## Implementation phases

## Phase 1: Specification and contracts

Step 1. Create component spec
- file: spec/yaha/SPEC-broker-connector.md
- define purpose, role, behavior, configuration, error handling, open questions

Step 2. Define adapter contracts
- source adapter interface (connect, subscribe, onPublish callback, close)
- target publish contract through existing YAHA MQTT client publish boundary
- component runtime interface

Deliverable:
- stable contracts before coding

## Phase 2: Source side (HTTP broker)

Step 3. Implement HTTP source adapter
- use HTTP MQTT interface 1.0 contract
- support connect, subscribe, pingreq, disconnect
- expose callback for received Message

Step 4. Implement source lifecycle manager
- startup sequence connect -> subscribe
- reconnect and re-subscribe on failure

Deliverable:
- source side receives live messages and emits Message callbacks

## Phase 3: Target side (MQTT receiver)

Step 5. Reuse and wire standard YAHA MQTT client
- instantiate existing generic YAHA MQTT client for receiver broker
- configure connect/reconnect/keepalive through existing runtime config
- expose publish(Message) call path to connector component

Step 6. Add relay component
- on source message callback call target publish
- apply retry policy
- maintain counters: received, forwarded, failed

Deliverable:
- end-to-end forwarding core works

## Phase 4: Composition and runtime

Step 7. Main composition
- parse config
- create adapters and component
- wire callback chain
- run lifecycle

Step 8. Add runtime shutdown handling
- graceful close order: source adapter, YAHA MQTT client, component

Deliverable:
- executable connector binary

## Phase 5: Tests

Step 9. Unit tests
- source adapter request/response checks against interface 1.0
- component forwarding and retry behavior
- counter correctness

Step 10. Integration tests
- test setup with one HTTP broker source and one MQTT target broker
- publish messages to source topics and verify arrival on target topics
- verify reconnect and re-subscribe after source restart
- verify target temporary outage and retry behavior

Step 11. Soak test
- long-running relay stability test with periodic disconnect faults

## Minimal acceptance criteria

1. Connector subscribes on source HTTP broker and receives messages.
2. Connector publishes every received message to receiver MQTT broker.
3. Source reconnect with re-subscribe works.
4. Target reconnect works.
5. Integration tests prove message forwarding.

## Open decisions before implementation

1. QoS mapping rule
- keep source qos as-is or normalize to qos 1 on target

2. Retain flag mapping
- forward retain unchanged or force retain false

3. Topic namespace policy
- forward topic unchanged or prepend relay prefix

4. Failure policy
- drop after retries or block pipeline until success

5. Runtime form
- standalone connector executable only, or library plus executable

## Dependency order summary

Step 1 -> Step 2 -> Step 3 -> Step 4 -> Step 5 -> Step 6 -> Step 7 -> Step 8 -> Step 9 -> Step 10 -> Step 11

Contract steps first. Runtime composition after adapters. Tests after full wiring.