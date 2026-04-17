# MQTT 5.0 Broker – Implementation Plan

Module structure for a fully specification-compliant MQTT 5.0 broker. Each module produces self-contained, independent code. The order follows the dependency chain — each module builds on the ones before it.

---

## 1. Data Models

*Pure data structures, no logic. No external dependencies.*

- 1.1 Primitive Types
  - 1.1.1 Variable Byte Integer
  - 1.1.2 UTF-8 String
  - 1.1.3 UTF-8 String Pair
  - 1.1.4 Binary Data
  - 1.1.5 Two Byte Integer
  - 1.1.6 Four Byte Integer
- 1.2 Reason Codes
  - 1.2.1 Reason Code Enum / Constants (all 39 values)
  - 1.2.2 Reason Code Classification (Success / Error)
- 1.3 Property Definitions
  - 1.3.1 Property Identifier Enum (all 27 IDs)
  - 1.3.2 Property-to-Data-Type Mapping
  - 1.3.3 Property-to-Packet-Type Mapping (where allowed)
- 1.4 Packet Structures
  - 1.4.1 CONNECT
  - 1.4.2 CONNACK
  - 1.4.3 PUBLISH
  - 1.4.4 PUBACK
  - 1.4.5 PUBREC
  - 1.4.6 PUBREL
  - 1.4.7 PUBCOMP
  - 1.4.8 SUBSCRIBE
  - 1.4.9 SUBACK
  - 1.4.10 UNSUBSCRIBE
  - 1.4.11 UNSUBACK
  - 1.4.12 PINGREQ
  - 1.4.13 PINGRESP
  - 1.4.14 DISCONNECT
  - 1.4.15 AUTH
- 1.5 Message Model
  - 1.5.1 Message (Topic, Payload, QoS, Retain, Properties)
  - 1.5.2 Will Message Model
- 1.6 Subscription Model
  - 1.6.1 Subscription (Topic Filter, QoS, Options, Identifier)
  - 1.6.2 Subscription Options (No Local, Retain As Published, Retain Handling)
  - 1.6.3 Shared Subscription (Group, Topic Filter)
- 1.7 Session Model
  - 1.7.1 Session State (Client ID, Subscriptions, Expiry)
  - 1.7.2 Inflight Entry (Packet ID, Message, QoS Level, Timestamp)

---

## 2. Protocol Codec

*Serialization and deserialization of all MQTT packets. Depends on: 1.*

- 2.1 Primitive Type Codec
  - 2.1.1 Variable Byte Integer Encoder
  - 2.1.2 Variable Byte Integer Decoder
  - 2.1.3 UTF-8 String Encoder / Decoder
  - 2.1.4 Binary Data Encoder / Decoder
  - 2.1.5 Integer Encoder / Decoder (2 Byte, 4 Byte)
- 2.2 Properties Codec
  - 2.2.1 Properties Encoder (list → bytes)
  - 2.2.2 Properties Decoder (bytes → list)
  - 2.2.3 Property Validation (type, allowed packet, duplicates)
- 2.3 Fixed Header Codec
  - 2.3.1 Fixed Header Encoder
  - 2.3.2 Fixed Header Decoder (type + flags + remaining length)
- 2.4 CONNECT Codec
- 2.5 CONNACK Codec
- 2.6 PUBLISH Codec
- 2.7 ACK Packet Codec (PUBACK, PUBREC, PUBREL, PUBCOMP)
- 2.8 SUBSCRIBE / SUBACK Codec
- 2.9 UNSUBSCRIBE / UNSUBACK Codec
- 2.10 PINGREQ / PINGRESP Codec
- 2.11 DISCONNECT Codec
- 2.12 AUTH Codec
- 2.13 Packet Reader
  - 2.13.1 Determine packet type from Fixed Header
  - 2.13.2 Route to the appropriate decoder
  - 2.13.3 Reject unknown / reserved packet types

---

## 3. Topic Engine

*Validation and matching of topic names and filters. Depends on: 1.*

- 3.1 Topic Validator
  - 3.1.1 Topic Name Validation (no wildcards, valid UTF-8, length)
  - 3.1.2 Topic Filter Validation (wildcard position, syntax)
  - 3.1.3 System Topic Detection (`$` prefix)
- 3.2 Subscription Trie
  - 3.2.1 Trie data structure (node per topic level)
  - 3.2.2 Insert subscription
  - 3.2.3 Remove subscription
  - 3.2.4 Remove all subscriptions for a client
- 3.3 Topic Matcher
  - 3.3.1 Exact match
  - 3.3.2 Single-level wildcard (`+`) matching
  - 3.3.3 Multi-level wildcard (`#`) matching
  - 3.3.4 System topic exclusion from wildcard searches

---

## 4. In-Memory Store

*Runtime state storage for the broker. Depends on: 1, 3.*

- 4.1 Subscription Store
  - 4.1.1 Store subscription (Client ID + Subscription)
  - 4.1.2 Remove subscription
  - 4.1.3 Retrieve all subscribers for a topic (via Trie)
  - 4.1.4 Remove all subscriptions for a session
- 4.2 Retained Message Store
  - 4.2.1 Store / overwrite retained message
  - 4.2.2 Delete retained message (empty payload)
  - 4.2.3 Find matching retained messages for a topic filter
- 4.3 Session Store
  - 4.3.1 Create session
  - 4.3.2 Load session
  - 4.3.3 Delete session
  - 4.3.4 Retrieve all expired sessions
- 4.4 Inflight Store
  - 4.4.1 Create inflight entry (QoS 1/2)
  - 4.4.2 Update inflight entry (state progression)
  - 4.4.3 Remove inflight entry (on completion)
  - 4.4.4 Retrieve all open entries for a session
  - 4.4.5 Packet ID registry (in use / free)

---

## 5. QoS Engine

*State machines for guaranteed message delivery. Depends on: 1, 4.*

- 5.1 Packet Identifier Manager
  - 5.1.1 Allocate a free Packet ID (non-zero, unique per session)
  - 5.1.2 Release a Packet ID
  - 5.1.3 Separate ID spaces (inbound / outbound)
- 5.2 QoS 1 State Machine
  - 5.2.1 Inbound: receive PUBLISH → send PUBACK
  - 5.2.2 Outbound: send PUBLISH → await PUBACK → complete
  - 5.2.3 Retransmission (set DUP flag)
- 5.3 QoS 2 State Machine
  - 5.3.1 Inbound: PUBLISH → PUBREC → await PUBREL → PUBCOMP
  - 5.3.2 Outbound: PUBLISH → await PUBREC → PUBREL → await PUBCOMP
  - 5.3.3 Duplicate detection (already-received Packet IDs)
  - 5.3.4 Retransmission per phase

---

## 6. Network Layer

*Raw TCP communication. No MQTT knowledge. Depends on: —*

- 6.1 TCP Listener
  - 6.1.1 Open server socket (port, IPv4/IPv6)
  - 6.1.2 Accept loop (accept new connections)
  - 6.1.3 Create and hand off connection object
- 6.2 Read Buffer
  - 6.2.1 Buffer incoming byte stream
  - 6.2.2 Detect complete packets using Remaining Length
  - 6.2.3 Hold incomplete packets until next data fragment arrives
- 6.3 Write Queue
  - 6.3.1 Enqueue outgoing serialized packets
  - 6.3.2 Drain queue to socket asynchronously
  - 6.3.3 Signal backpressure when queue is full

---

## 7. Connection Handler

*Manages the lifecycle of a single client connection. Depends on: 2, 5, 6.*

- 7.1 Connection State Machine
  - 7.1.1 States: Connecting → Connected → Disconnecting → Closed
  - 7.1.2 Enforce CONNECT as first packet
  - 7.1.3 Detect and reject duplicate CONNECT
  - 7.1.4 Detect abrupt connection loss
- 7.2 Keep-Alive Timer
  - 7.2.1 Compute deadline (1.5 × Keep Alive)
  - 7.2.2 Reset timer on every incoming packet
  - 7.2.3 Close connection on timeout
- 7.3 Topic Alias Table
  - 7.3.1 Store alias → topic mapping (inbound)
  - 7.3.2 Store topic → alias mapping (outbound)
  - 7.3.3 Reset table on connection close
  - 7.3.4 Enforce Topic Alias Maximum
- 7.4 Receive Maximum Controller
  - 7.4.1 Track inflight count per connection
  - 7.4.2 Pause sending when limit is reached
  - 7.4.3 Resume sending after acknowledgment

---

## 8. Authentication Module

*Verifies client identity at connection time. Depends on: 1, 7.*

- 8.1 Authenticator Interface
  - 8.1.1 Abstract interface (credentials → result)
  - 8.1.2 Plugin / callback mechanism
- 8.2 Username / Password Authenticator
  - 8.2.1 Read credentials from CONNECT
  - 8.2.2 Validate against configured backend
- 8.3 Enhanced Auth Handler
  - 8.3.1 Read Authentication Method from CONNECT
  - 8.3.2 Send AUTH packet (Reason 0x18 – Continue)
  - 8.3.3 Receive AUTH packet and forward data
  - 8.3.4 Complete multi-step handshake
  - 8.3.5 Re-authentication during active session (Reason 0x19)
- 8.4 Anonymous Access
  - 8.4.1 Allow / deny connection without credentials (configurable)

---

## 9. Authorization Module

*Checks permissions for publish and subscribe actions. Depends on: 1, 8.*

- 9.1 ACL Engine
  - 9.1.1 ACL rule structure (principal, topic pattern, action)
  - 9.1.2 Check publish permission
  - 9.1.3 Check subscribe permission
  - 9.1.4 Wildcard support in ACL rules
- 9.2 ACL Loader
  - 9.2.1 Load ACL rules from configuration
  - 9.2.2 Reload rules at runtime

---

## 10. Session Manager

*Controls session lifecycle and persistence coordination. Depends on: 4, 7, 8.*

- 10.1 Session Lifecycle Controller
  - 10.1.1 Create new session (Clean Start = 1)
  - 10.1.2 Resume existing session (Clean Start = 0)
  - 10.1.3 Determine Session Present flag for CONNACK
  - 10.1.4 Retain or discard session on disconnect
- 10.2 Session Takeover Handler
  - 10.2.1 Detect existing connection with same Client ID
  - 10.2.2 Close old connection with Reason Code 0x8E
  - 10.2.3 Transfer session state to new connection
- 10.3 Session Expiry Scheduler
  - 10.3.1 Start expiry timer per session
  - 10.3.2 Cancel timer on reconnect
  - 10.3.3 Clean up expired session and all associated data

---

## 11. Will Manager

*Stores and publishes Will Messages. Depends on: 1, 4, 10.*

- 11.1 Will Store
  - 11.1.1 Persist Will data in session
  - 11.1.2 Load Will data from session
  - 11.1.3 Delete Will data (after publish or normal disconnect)
- 11.2 Will Delay Timer
  - 11.2.1 Start delay timer after connection loss
  - 11.2.2 Cancel timer if client reconnects before expiry
  - 11.2.3 Publish immediately if session expires before Will Delay
- 11.3 Will Publisher
  - 11.3.1 Feed Will Message into the routing pipeline
  - 11.3.2 Suppress Will on normal DISCONNECT (Reason 0x00)
  - 11.3.3 Publish Will on DISCONNECT with Reason 0x04

---

## 12. Message Router

*Dispatches incoming messages to all matching subscribers. Depends on: 3, 4, 5, 9, 10, 11.*

- 12.1 Inbound Publish Processor
  - 12.1.1 Resolve Topic Alias
  - 12.1.2 Check publish authorization
  - 12.1.3 Store retained message (if RETAIN = 1)
  - 12.1.4 Retrieve subscriber list from Subscription Store
- 12.2 Subscriber Fanout
  - 12.2.1 Deliver message to each subscriber with correct QoS (downgrade if needed)
  - 12.2.2 Apply No Local filter
  - 12.2.3 Apply Retain As Published flag
  - 12.2.4 Attach Subscription Identifier property
- 12.3 Offline Queue
  - 12.3.1 Buffer message for disconnected session
  - 12.3.2 Deliver queued messages on reconnect
  - 12.3.3 Enforce queue size limit
- 12.4 Message Expiry Controller
  - 12.4.1 Compute expiry timestamp (arrival time + interval)
  - 12.4.2 Discard expired messages before delivery
  - 12.4.3 Set remaining time in outbound PUBLISH
- 12.5 Shared Subscription Dispatcher
  - 12.5.1 Retrieve active members of a subscription group
  - 12.5.2 Deliver message to exactly one member (round-robin)
  - 12.5.3 Clean up group when empty

---

## 13. Persistence Adapter

*Writes and reads broker state to durable storage. Depends on: 4.*

- 13.1 Session Persistence Adapter
  - 13.1.1 Serialize and write session to storage
  - 13.1.2 Read and deserialize session from storage
  - 13.1.3 Delete session from storage
- 13.2 Retained Message Persistence Adapter
  - 13.2.1 Write retained message to storage
  - 13.2.2 Load all retained messages at startup
  - 13.2.3 Delete retained message from storage
- 13.3 Inflight State Persistence Adapter
  - 13.3.1 Write inflight entries for a session to storage
  - 13.3.2 Restore inflight entries on restart

---

## 14. Transport Extensions

*Alternative transports beyond plain TCP. Depends on: 6.*

- 14.1 TLS Transport — **NOT IMPLEMENTED**
  - Provides encrypted MQTTS (port 8883) and WSS transport.
  - Requires an external TLS library (e.g. OpenSSL, mbedTLS).
  - Recommended alternative: use a reverse proxy (nginx, HAProxy, stunnel) for TLS termination.
- 14.2 WebSocket Transport ✓
  - 14.2.1 HTTP upgrade handshake (`Upgrade: websocket`) — validates RFC 6455 headers, computes `Sec-WebSocket-Accept` via SHA-1 + Base64 (no external dependencies)
  - 14.2.2 WebSocket frame encoder / decoder — supports 1-byte, 16-bit, and 64-bit payload length; handles client-side masking; encodes server-side frames unmasked
  - 14.2.3 Extract MQTT payload from WebSocket frame — binary frames carry MQTT packet bytes directly

---

## 15. Broker Orchestrator

*Wires all modules together and controls startup / shutdown. Depends on: all previous.*

- 15.1 Configuration
  - 15.1.1 Load and validate configuration file
  - 15.1.2 Port configuration (MQTT, MQTTS, WS, WSS)
  - 15.1.3 Broker parameters (limits, timeouts, feature flags)
- 15.2 Component Wiring
  - 15.2.1 Instantiate modules and inject dependencies
  - 15.2.2 Bind persistence adapters to in-memory stores
  - 15.2.3 Bind auth / ACL module to connection handler
- 15.3 Startup / Shutdown Controller
  - 15.3.1 Ordered startup (load persistence → fill stores → open listeners)
  - 15.3.2 Ordered shutdown (close listeners → disconnect clients → flush persistence)
  - 15.3.3 Signal handler (SIGTERM, SIGINT)

---

## 16. Monitoring

*Runtime observability of the broker. Depends on: 4, 12, 15.*

- 16.1 Statistics Collector
  - 16.1.1 Count connected clients
  - 16.1.2 Count message throughput (inbound / outbound)
  - 16.1.3 Count active subscriptions
  - 16.1.4 Count retained messages
  - 16.1.5 Measure uptime
- 16.2 $SYS Topic Publisher
  - 16.2.1 Publish statistics periodically to `$SYS` topics
  - 16.2.2 Configurable publish interval
  - 16.2.3 Exclude `$SYS` topics from normal wildcard subscriptions
