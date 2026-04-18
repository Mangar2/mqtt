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

---

## 17. Broker Concurrency Layer

*Thread safety for the Broker and all shared mutable state. Currently the only mutex in the codebase is in `WriteQueue`. Every `ClientHandler::run()` executes on a detached thread, but `register_connection()`, `unregister_connection()`, `route_message()`, `SessionManager`, `WillPublisher`, all stores, and all Broker accessors are completely unprotected. This module adds synchronisation so that concurrent client threads cannot corrupt shared state. Depends on: 4, 10, 11, 12, 15.*

- 17.1 Broker Mutex
  - 17.1.1 Add a `std::shared_mutex` to `Broker` guarding all mutable shared state
  - 17.1.2 `register_connection()` and `unregister_connection()` acquire exclusive lock
  - 17.1.3 `route_message()` acquires exclusive lock (writes to stores, reads connection map)
  - 17.1.4 `tick()` acquires exclusive lock (publishes `$SYS`, runs housekeeping)
- 17.2 Thread-Safe Store Access
  - 17.2.1 Remove direct public store accessors (`subscription_store()`, `retained_message_store()`, `inflight_store()`) from `Broker` — external code must use facade methods (see Module 18/19) instead of reaching into internals
  - 17.2.2 Internal store access within already-locked Broker methods remains direct (no double locking)
- 17.3 Thread-Safe Session Manager Access
  - 17.3.1 `Broker::handle_connect()` wraps `SessionManager::handle_connect()` under exclusive lock
  - 17.3.2 `Broker::handle_disconnect()` wraps `SessionManager::handle_disconnect()` under exclusive lock
  - 17.3.3 `Broker::handle_connection_lost()` wraps will and session teardown under exclusive lock

---

## 18. Broker Facade — Connect / Disconnect

*Complete, thread-safe, high-level methods on `Broker` that encapsulate the multi-step connect and disconnect workflows. The client handler calls one method instead of orchestrating 5+ sub-calls. Depends on: 8, 10, 11, 15, 17.*

- 18.1 Connect Result
  - 18.1.1 `ConnectResult` struct: `session_present`, `reason_code`, `connack_properties`, `client_id`
  - 18.1.2 Encapsulates the full outcome of the handshake in one return value
- 18.2 `Broker::handle_connect(ConnectPacket, close_fn) → ConnectResult`
  - 18.2.1 Authenticate via `IAuthenticator` (Module 8) — return failure reason on auth error
  - 18.2.2 Open / resume session via `SessionManager` (Module 10)
  - 18.2.3 Store Will Message if present (Module 11) — extract `WillMessage` from CONNECT internally
  - 18.2.4 Build CONNACK properties (Receive Maximum, Topic Alias Maximum from config)
  - 18.2.5 Register connection send callback (see Module 20 outbound queue)
  - 18.2.6 Flush offline queue for resumed sessions
  - 18.2.7 All steps under exclusive lock (Module 17)
- 18.3 `Broker::handle_disconnect(client_id, reason_code, expiry_override, now)`
  - 18.3.1 Call `WillPublisher::on_disconnect()` (suppresses will on reason 0x00, publishes on 0x04)
  - 18.3.2 Unregister connection from active map
  - 18.3.3 Call `SessionManager::handle_disconnect()`
  - 18.3.4 All steps under exclusive lock
- 18.4 `Broker::handle_connection_lost(client_id, now)`
  - 18.4.1 Call `WillPublisher::on_connection_lost()`
  - 18.4.2 Unregister connection from active map
  - 18.4.3 Call `SessionManager::handle_disconnect()`
  - 18.4.4 All steps under exclusive lock

---

## 19. Broker Facade — Subscribe / Unsubscribe / Publish

*Complete, thread-safe, high-level methods on `Broker` for the three most common per-packet operations. Removes direct store access from the client handler. Depends on: 4, 9, 12, 15, 17.*

- 19.1 `Broker::handle_subscribe(client_id, SubscribePacket) → SubackPacket`
  - 19.1.1 Iterate filters: store each subscription in `SubscriptionStore`
  - 19.1.2 Check subscribe authorisation via `AclEngine` per filter
  - 19.1.3 Collect reason codes (granted QoS or error)
  - 19.1.4 Retrieve and deliver retained messages per filter
  - 19.1.5 Build and return `SubackPacket`
  - 19.1.6 All steps under exclusive lock
- 19.2 `Broker::handle_unsubscribe(client_id, UnsubscribePacket) → UnsubackPacket`
  - 19.2.1 Remove each filter from `SubscriptionStore`
  - 19.2.2 Build and return `UnsubackPacket` with per-filter reason codes
  - 19.2.3 All steps under exclusive lock
- 19.3 `Broker::handle_publish(msg, client_id, username, alias_table)`
  - 19.3.1 Wraps `route_message()` with inbound statistics increment
  - 19.3.2 Under exclusive lock
  - 19.3.3 Replaces old `route_message()` public API

---

## 20. Outbound Message Queue

*Thread-safe per-client message queue that decouples the publishing thread from the receiving client's QoS state. Currently `SendFn` is a closure that captures per-client `Qos1StateMachine`, `Qos2StateMachine`, `ReceiveMaximum`, and `WriteQueue` by reference — but it is called from the publishing client's thread, causing data races on all these objects. This module moves outbound QoS processing to the receiving client's own thread. Depends on: 1, 6, 17.*

- 20.1 `OutboundQueue` Class
  - 20.1.1 Thread-safe FIFO queue of `Message` objects (mutex + condition variable)
  - 20.1.2 `push(Message)` — called from any thread (the broker's `SendFn`)
  - 20.1.3 `try_pop() → optional<Message>` — non-blocking poll, called by client thread
  - 20.1.4 `stop()` — signal shutdown, unblock any waiting consumer
  - 20.1.5 Configurable max queue depth (backpressure: drop or block)
- 20.2 Broker Integration
  - 20.2.1 `Broker::register_connection()` takes a `shared_ptr<OutboundQueue>` instead of a raw `SendFn`
  - 20.2.2 `MessageRouter` fanout pushes `Message` into the target client's `OutboundQueue`
  - 20.2.3 No QoS processing on the publishing thread — just queue the message

---

## 21. Client Session Context

*Bundles all per-connection state into a single object with per-packet handler methods. Eliminates the 8+ loose local variables in the current monolithic `run()`. Depends on: 2, 5, 7, 20.*

- 21.1 `ClientSession` Class
  - 21.1.1 Owns: `PacketIdManager`, `Qos1StateMachine`, `Qos2StateMachine`, `ReceiveMaximum`, `TopicAliasTable`, `KeepAliveTimer`, `ConnectionStateMachine`, `EnhancedAuthHandler`
  - 21.1.2 Owns: `shared_ptr<OutboundQueue>` (Module 20)
  - 21.1.3 Holds: `client_id`, `username` (copied from CONNECT)
  - 21.1.4 Constructor takes CONNECT result parameters (keep-alive, receive-max, alias-max, etc.)
- 21.2 Inbound Packet Handlers (return encoded response bytes)
  - 21.2.1 `on_publish(PublishPacket) → vector<WriteBuffer>` — QoS 0: nothing; QoS 1: PUBACK; QoS 2: PUBREC; returns message for routing
  - 21.2.2 `on_puback(PubackPacket)` — forwards to `Qos1StateMachine`, releases `ReceiveMaximum` slot
  - 21.2.3 `on_pubrec(PubrecPacket) → WriteBuffer` — forwards to `Qos2StateMachine`, returns PUBREL
  - 21.2.4 `on_pubrel(PubrelPacket) → WriteBuffer` — forwards to `Qos2StateMachine`, returns PUBCOMP
  - 21.2.5 `on_pubcomp(PubcompPacket)` — forwards to `Qos2StateMachine`, releases `ReceiveMaximum` slot
  - 21.2.6 `on_auth(AuthPacket) → AuthHandlerResult` — forwards to `EnhancedAuthHandler`
- 21.3 Outbound Delivery (drains `OutboundQueue` on client's own thread)
  - 21.3.1 `drain_outbound() → vector<WriteBuffer>` — pops messages from queue, applies QoS initiation (`initiate_publish`), respects `ReceiveMaximum`, returns encoded PUBLISH packets
  - 21.3.2 All QoS state modification happens here — on the owning thread, no races

---

## 22. Broker Housekeeping

*Periodic maintenance tasks integrated into `Broker::tick()`. Currently `tick()` only publishes `$SYS` stats. Missing: will publish due, session expiry cleanup, QoS retransmission. Also: the main loop in `main.cpp` is a busy-spin with no sleep. Depends on: 5, 10, 11, 15, 17.*

- 22.1 Will Publish Due
  - 22.1.1 Call `WillPublisher::publish_due(now)` inside `Broker::tick()`
  - 22.1.2 Publishes will messages whose delay timer has expired
- 22.2 Session Expiry Cleanup
  - 22.2.1 Call `SessionManager::cleanup_expired(now)` inside `Broker::tick()` — returns list of expired Client IDs
  - 22.2.2 `SessionManager::cleanup_expired()` handles all internal cleanup (subscriptions, inflight state, session record) — Broker does **not** touch stores directly
  - 22.2.3 Broker iterates the returned Client ID list and calls `WillPublisher::on_session_expired(client_id)` for each — this is the only Broker-level action
- 22.3 QoS Retransmission Timer
  - 22.3.1 Retransmission is owned by `ClientSession` (Module 21), **not** by `Broker::tick()` — QoS state machines live in `ClientSession` and must only be accessed from the client's own thread (see 21.3.2)
  - 22.3.2 `ClientSession::drain_outbound()` checks retransmission deadlines alongside the normal OutboundQueue drain: for each outstanding QoS 1/2 entry whose timestamp exceeds the retransmit timeout, call `retransmit()` on the owning state machine
  - 22.3.3 Set DUP flag on retransmitted PUBLISH (already implemented in state machines)
  - 22.3.4 Return retransmitted packets together with newly queued packets from `drain_outbound()`
  - 22.3.5 Retransmit timeout is a configurable `BrokerConfig` value (e.g. 20 s) passed to `ClientSession` at construction
- 22.4 Main Loop Throttle
  - 22.4.1 Add configurable tick interval (e.g. 100 ms) in main loop
  - 22.4.2 Use `std::this_thread::sleep_for()` or condition variable to avoid busy spin

---

## 23. Accept Loop Thread Management

*Replace `std::thread(...).detach()` in the accept loop with tracked threads that are properly joined on shutdown. Currently, on broker shutdown, detached client threads continue running with dangling references to the broker and its stores — undefined behaviour. Depends on: 15, 17.*

- 23.1 Client Thread Registry
  - 23.1.1 `Broker` maintains a `vector<jthread>` (or similar) of active client threads
  - 23.1.2 Each accept spawns a `jthread` that is added to the registry
  - 23.1.3 Completed threads are periodically removed (e.g. in `tick()` or on accept)
- 23.2 Graceful Client Shutdown
  - 23.2.1 On `Broker::shutdown()`: set running flag to false
  - 23.2.2 Close all TCP listeners (breaks accept loops)
  - 23.2.3 Signal all `OutboundQueue`s to stop (wakes blocked clients)
  - 23.2.4 Join all client threads with a timeout
  - 23.2.5 Force-close remaining connections after timeout

---

## 24. Lean Client Handler

*Thin orchestration layer — the only module that touches I/O. All business logic has been pushed into Broker facades (Modules 18/19) and ClientSession (Module 21). This module reads packets, delegates to the appropriate facade, and writes responses. Target: ~150–200 lines. Depends on: all previous.*

- 24.1 Transport Setup
  - 24.1.1 Detect and complete WebSocket upgrade if applicable
  - 24.1.2 Set socket receive timeout for keep-alive polling
  - 24.1.3 Create `StreamBuffer` (inbound) and `WriteQueue` (outbound)
  - 24.1.4 Start drain thread for `WriteQueue`
- 24.2 CONNECT Handshake
  - 24.2.1 Read first packet from `StreamBuffer`; reject non-CONNECT
  - 24.2.2 Call `Broker::handle_connect()` — single method call (Module 18)
  - 24.2.3 Handle multi-step enhanced auth loop if needed (AUTH packets)
  - 24.2.4 Encode and send CONNACK; on failure send error CONNACK and return
  - 24.2.5 Construct `ClientSession` from connect result (Module 21)
- 24.3 Per-Packet Dispatch Loop
  - 24.3.1 Read next chunk from transport; append to `StreamBuffer`
  - 24.3.2 On received data: reset `KeepAliveTimer` (via `ClientSession`)
  - 24.3.3 On keep-alive expiry: send DISCONNECT (0x8D), break
  - 24.3.4 Drain `OutboundQueue` via `ClientSession::drain_outbound()`, enqueue results to `WriteQueue`
  - 24.3.5 Decode complete packets and dispatch:
    - PUBLISH → `ClientSession::on_publish()` + `Broker::handle_publish()`; send QoS response
    - SUBSCRIBE → `Broker::handle_subscribe()` (Module 19); send SUBACK (retained messages are delivered inside `handle_subscribe` via `MessageRouter::deliver_retained` → `OutboundQueue`; no extra action in ClientHandler)
    - UNSUBSCRIBE → `Broker::handle_unsubscribe()` (Module 19); send UNSUBACK
    - PUBACK / PUBREC / PUBREL / PUBCOMP → `ClientSession::on_*()` methods; send response
    - PINGREQ → encode and send PINGRESP
    - DISCONNECT → record reason, break
    - AUTH → `ClientSession::on_auth()`; send response
  - 24.3.6 Check `broker.is_running()` each iteration (graceful shutdown)
- 24.4 Teardown
  - 24.4.1 Stop `WriteQueue`; drain thread joins via `jthread` destructor
  - 24.4.2 On clean disconnect: call `Broker::handle_disconnect()` (Module 18)
  - 24.4.3 On connection loss: call `Broker::handle_connection_lost()` (Module 18)

---

## 25. Broker.cpp Refactoring — Extract Misplaced Logic

*`broker.cpp` currently contains helper functions and inline logic that violate module boundaries. This module extracts each piece into its owning module and fixes functional gaps caused by the shortcuts. Depends on: 1, 8, 11, 12, 19.*

- 25.1 Retained Message Delivery via MessageRouter
  - 25.1.1 **Problem:** `Broker::handle_subscribe()` directly queries `retained_store_->find()` and invokes `SendFn` via the `active_connections_` map, bypassing `MessageRouter` entirely. Consequences: `RetainHandling` subscription option is ignored (messages are always sent, even when the option says "don't send on subscribe" or "send only if subscription is new"); QoS downgrade per subscription is not applied; `MessageExpiryController` is not consulted (expired retained messages are delivered); `SubscriptionIdentifier` property is not attached; `RetainAsPublished` flag is not applied.
  - 25.1.2 Add a method `MessageRouter::deliver_retained(client_id, topic_filter, subscription)` that: retrieves matching retained messages from `RetainedMessageStore`; respects `RetainHandling` (0 = always, 1 = only if new, 2 = never); passes each message through `SubscriberFanout::apply_subscription_rules()` (QoS downgrade, Retain As Published, Subscription Identifier); passes each message through `MessageExpiryController` (discard expired); delivers via the existing `DeliverFn` callback.
  - 25.1.3 `Broker::handle_subscribe()` calls `message_router_->deliver_retained()` instead of accessing `retained_store_` and `active_connections_` directly. The `SubscriptionStore::store()` return value (new vs. updated) must be available so `RetainHandling::SendIfNew` can be evaluated.
  - 25.1.4 Remove `retained_store_` direct access from `handle_subscribe()`.
- 25.2 Move `extract_will_message()` to will_manager
  - 25.2.1 **Problem:** The free function `extract_will_message(WillData) → WillMessage` in the anonymous namespace of `broker.cpp` is a data-model transformation that belongs in the `will_manager` module.
  - 25.2.2 Move to `will_manager/will_message_util.h` (or integrate into `WillPublisher::on_connect()` so it accepts `WillData` directly instead of `WillMessage`).
  - 25.2.3 Update `Broker::complete_connect_success()` to call the moved function or pass `WillData` to `WillPublisher::on_connect()`.
- 25.3 Move `granted_qos_reason()` to data_model
  - 25.3.1 **Problem:** Maps `QoS → ReasonCode`. Both types live in `data_model`. The function is in the anonymous namespace of `broker.cpp`.
  - 25.3.2 Move to `data_model/reason_code/reason_code.h` as a free function `qos_to_granted_reason(QoS) → ReasonCode`.
- 25.4 Move `find_subscription_identifier()` to data_model
  - 25.4.1 **Problem:** Extracts the `SubscriptionIdentifier` property from a `SubscribePacket`. This is packet property parsing that belongs in the data model or codec layer.
  - 25.4.2 Move to `data_model/packet/subscribe_packets.h` as a free function (or member of `SubscribePacket`).
- 25.5 Move `password_to_binary()` to auth module
  - 25.5.1 **Problem:** Converts `std::string_view` to `BinaryData` for password handling. This is an auth utility sitting in `broker.cpp`.
  - 25.5.2 Move to `auth/authenticator.h` or `data_model/primitive/binary_data.h` as `BinaryData::from_string(std::string_view)`.
- 25.6 Remove `route_message()` compatibility wrapper
  - 25.6.1 **Problem:** `Broker::route_message()` is a one-line wrapper that forwards to `handle_publish()`. Module 19.3.3 states it replaces the old API, but the old method was kept.
  - 25.6.2 Remove `route_message()` from `Broker` public API.
  - 25.6.3 Update all callers (tests, SPEC.md) to use `handle_publish()` instead.
