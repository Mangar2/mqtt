# HTTP MQTT Full Command Flow Concept

## Purpose
Define the target runtime flow for a complete HTTP-exposed MQTT command set while preserving the existing browser publish path.
The HTTP MQTT client must act as a transport and session bridge, not as an MQTT protocol reimplementation.

## Target Scope
Expose a complete HTTP-facing operation set for MQTT-style client behavior:
- connect
- subscribe
- publish
- receive
- ping
- unsubscribe
- disconnect

QoS levels in scope:
- QoS 0
- QoS 1
- QoS 2

## Core Principle
The broker remains the single source of truth for MQTT behavior.
The HTTP MQTT client forwards intent and returns broker outcomes.
No local simulation of broker protocol semantics.

## Roles
HTTP caller:
- drives session lifecycle and operations through HTTP requests

HTTP MQTT client:
- validates request shape
- maps HTTP operations to broker-facing MQTT operations
- tracks HTTP session to broker session association
- forwards broker responses and events back to HTTP caller
- preserves compatibility routes used by browser GUI

MQTT broker:
- owns protocol semantics, session state, subscription state, retained handling, delivery guarantees, keepalive evaluation, and QoS handshake behavior

## End-to-End Runtime Flow
1. HTTP caller sends connect request.
2. HTTP MQTT client establishes or binds a broker-backed MQTT session.
3. Client receives broker connect outcome and returns it to HTTP caller, including session identity for subsequent calls.
4. HTTP caller issues subscribe, publish, receive, ping, unsubscribe operations bound to that session identity.
5. HTTP MQTT client forwards each operation to broker-facing MQTT runtime.
6. Broker executes MQTT semantics and returns acknowledgements or messages.
7. HTTP MQTT client maps broker outcomes to HTTP responses without changing MQTT semantics.
8. HTTP caller sends disconnect.
9. HTTP MQTT client closes broker session and releases session association.

## Operation Flow by Command

### Connect
1. HTTP caller requests connect with desired MQTT session options.
2. HTTP MQTT client creates or resumes a broker session through existing MQTT runtime.
3. Broker decides accept or reject.
4. HTTP MQTT client returns broker outcome and session reference to caller.

### Subscribe
1. HTTP caller submits topic filters and requested QoS for an existing session.
2. HTTP MQTT client forwards subscribe request unchanged in intent.
3. Broker evaluates permissions and subscription validity.
4. Broker returns granted or rejected QoS results.
5. HTTP MQTT client returns these results to HTTP caller.

### Publish
1. HTTP caller submits publish message for an existing session.
2. HTTP MQTT client forwards topic, payload, retain, dup, qos, and packet identity intent to broker runtime.
3. Broker performs publish processing and QoS handshake decisions.
4. HTTP MQTT client returns broker acknowledgement outcome to HTTP caller.

### Receive
1. HTTP caller requests incoming messages for an existing session.
2. HTTP MQTT client reads pending deliveries originating from broker for that session.
3. Delivery order and visibility follow broker-driven message flow.
4. HTTP MQTT client returns received messages and delivery metadata to caller.

### Ping
1. HTTP caller requests liveness activity for an existing session.
2. HTTP MQTT client triggers broker-facing keepalive activity through existing MQTT runtime.
3. Broker-side keepalive outcome is returned.
4. HTTP MQTT client reports success or failure to caller.

### Unsubscribe
1. HTTP caller submits unsubscribe request for an existing session.
2. HTTP MQTT client forwards to broker runtime.
3. Broker removes subscription state and returns unsubscribe result.
4. HTTP MQTT client returns broker result to caller.

### Disconnect
1. HTTP caller requests disconnect for an existing session.
2. HTTP MQTT client closes broker session using existing MQTT runtime.
3. Broker finalizes session according to its policy.
4. HTTP MQTT client confirms completion and invalidates session reference.

## QoS Ownership and Pass-Through Rules
QoS 0:
- broker decides best-effort delivery behavior
- HTTP MQTT client only forwards outcome

QoS 1:
- broker controls PUBACK semantics and retransmission logic
- HTTP MQTT client exposes broker acknowledgements

QoS 2:
- broker controls full exactly-once handshake semantics
- HTTP MQTT client must not emulate handshake state
- HTTP responses reflect broker-driven progression and results

## Browser Publish Compatibility Requirement
Existing browser GUI publish behavior remains intact.
Current compatibility routes continue to work unchanged in behavior.
Full command set extension is additive and must not regress:
- browser publish request acceptance
- browser publish response compatibility mode
- current publish route reliability and logging behavior

## Session Model
Each connect creates or binds a broker session reference exposed to HTTP caller.
All follow-up commands must reference this active session.
No command-level shadow state should replace broker session truth.
Session expiry and invalid session behavior must be mapped clearly to HTTP errors.

## Error Mapping Principles
- Request shape errors: rejected before broker call
- Broker rejection or protocol outcome: surfaced as broker-derived operation failure
- Transport/runtime failures between HTTP bridge and broker runtime: surfaced as internal operation failure

Error mapping must preserve distinction between caller error, broker decision, and bridge/runtime failure.

## Delivery and Receive Semantics
Receive returns only messages that belong to the connected broker session context.
Message content and ordering semantics originate from broker delivery behavior.
The bridge must not reorder or rewrite semantic message metadata.

## Observability Principles
Each operation should produce traceable operation-level logs that correlate:
- HTTP request
- broker-facing operation
- broker outcome

Log semantics remain additive and must not remove existing publish route observability.

## Acceptance Conditions
A complete HTTP session can execute this sequence against broker-backed behavior:
- connect
- subscribe
- publish
- receive
- ping
- unsubscribe
- disconnect

For publish and receive, QoS 0, 1, and 2 broker capabilities are exposed through the HTTP bridge without local protocol reimplementation.

Existing browser publish compatibility path remains fully functional.
