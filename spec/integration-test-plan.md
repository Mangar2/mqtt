# MQTT 5.0 Broker – Integration Test Plan

Hierarchical list of all integration tests for complete broker coverage according to the MQTT 5.0 specification (OASIS Standard). Basis: requirements catalog and implementation plan, not existing code.

Each test starts the broker as a black box and communicates exclusively over the MQTT protocol (TCP/WebSocket).

---

## 0. Prerequisites — Test Toolbox

All helper modules live in `test/integration_tests/helpers/`. They must be built and verified before any test category is started.

### 0.1 MQTT Client Helper (`mqtt_client.py`)
Wrapper around `paho-mqtt` providing high-level, blocking convenience methods for integration tests.
- 0.1.1 `connect(host, port, client_id, clean_start, keepalive, properties, username, password)` → returns CONNACK result (reason code, session present, all properties)
- 0.1.2 `disconnect(reason_code, properties)` → sends DISCONNECT, waits for clean close
- 0.1.3 `publish(topic, payload, qos, retain, properties)` → sends PUBLISH, waits for ACK (QoS 1/2), returns reason code
- 0.1.4 `subscribe(topic_filter, qos, options, subscription_id)` → sends SUBSCRIBE, returns SUBACK reason codes
- 0.1.5 `unsubscribe(topic_filter)` → sends UNSUBSCRIBE, returns UNSUBACK reason codes
- 0.1.6 `collect_messages(count, timeout)` → waits for N inbound PUBLISH, returns list of messages with all properties
- 0.1.7 `wait_for_disconnect(timeout)` → waits for server-initiated DISCONNECT, returns reason code and properties
- 0.1.8 Will Message configuration (topic, payload, qos, retain, delay, properties) — set before connect
- 0.1.9 Topic Alias support (outbound alias table, auto-resolve)
- 0.1.10 Context manager (`with MqttClient(...) as client:`) — auto-disconnect on exit

### 0.2 Raw TCP Helper (`raw_tcp.py`)
Low-level socket operations for malformed packet and robustness tests.
- 0.2.1 `send_bytes(host, port, data)` → open TCP, send raw bytes, return response bytes
- 0.2.2 `send_partial_connect(host, port)` → send truncated CONNECT packet, observe broker behavior
- 0.2.3 `open_idle_connection(host, port, duration)` → TCP connect, send nothing, measure timeout
- 0.2.4 `send_and_expect_close(host, port, data, timeout)` → send data, verify broker closes connection
- 0.2.5 `flood_connections(host, port, count)` → open N TCP connections simultaneously, return success/failure per connection
- 0.2.6 CONNECT packet builder — assemble valid/invalid CONNECT packets byte-by-byte (protocol name, version, flags, properties)
- 0.2.7 PUBLISH packet builder — assemble PUBLISH packets with configurable QoS, flags, topic, properties
- 0.2.8 Generic packet builder — assemble any MQTT fixed header + variable header + payload from parts

### 0.3 Assertions Helper (`assertions.py`)
Reusable assertion functions for test validation.
- 0.3.1 `assert_connack(result, reason_code, session_present)` — verify CONNACK fields
- 0.3.2 `assert_connack_property(result, property_id, expected_value)` — verify single CONNACK property
- 0.3.3 `assert_message(message, topic, payload, qos, retain)` — verify received PUBLISH content
- 0.3.4 `assert_message_property(message, property_id, expected_value)` — verify single message property
- 0.3.5 `assert_reason_code(actual, expected)` — compare reason codes with readable error output
- 0.3.6 `assert_disconnected(client, reason_code, timeout)` — verify broker-initiated DISCONNECT
- 0.3.7 `assert_no_message(client, timeout)` — verify NO message arrives within timeout
- 0.3.8 `assert_connection_closed(host, port, data, timeout)` — send data, verify TCP close

### 0.4 Broker Lifecycle Helper (`broker.py`)
Broker process management for tests that need restart/reconfigure.
- 0.4.1 `start_broker(config_overrides)` → build, start broker process, wait for reachable
- 0.4.2 `stop_broker(process)` → graceful SIGTERM, wait for exit
- 0.4.3 `restart_broker(process, config_overrides)` → stop + start, for persistence/recovery tests
- 0.4.4 `is_reachable(host, port, timeout)` → TCP health check

### 0.5 Verification and Smoke Test
- 0.5.1 paho-mqtt library available (import check)
- 0.5.2 MqttClient connects and disconnects successfully
- 0.5.3 MqttClient pub/sub QoS 0 roundtrip works
- 0.5.4 MqttClient pub/sub QoS 1 roundtrip works
- 0.5.5 MqttClient pub/sub QoS 2 roundtrip works
- 0.5.6 Raw TCP helper can send bytes and receive response
- 0.5.7 Assertions produce clear error messages on failure

---

## 1. Connection Lifecycle

### 1.1 CONNECT — Basic
- 1.1.1 Anonymous connect (no credentials) → CONNACK success
- 1.1.2 Connect with valid username/password → CONNACK success
- 1.1.3 Connect with invalid username/password → CONNACK 0x86 (Bad User Name or Password)
- 1.1.4 Connect with empty Client ID → Broker assigns Client ID (Assigned Client Identifier property in CONNACK)
- 1.1.5 Connect with explicit Client ID → same ID used throughout session
- 1.1.6 Connect with invalid protocol version (e.g. 0x03) → connection refused
- 1.1.7 Connect with invalid protocol name (not "MQTT") → connection closed
- 1.1.8 Connect with reserved header flags set → connection closed (Malformed Packet)

### 1.2 CONNECT — Properties
- 1.2.1 Session Expiry Interval = 0 → session discarded on disconnect
- 1.2.2 Session Expiry Interval > 0 → session persisted after disconnect
- 1.2.3 Session Expiry Interval = 0xFFFFFFFF → session never expires
- 1.2.4 Receive Maximum property → broker respects client-side inflight limit
- 1.2.5 Maximum Packet Size property → broker does not send oversized packets
- 1.2.6 Topic Alias Maximum property → broker respects alias limit from client
- 1.2.7 Request Problem Information = 0 → broker omits Reason String / User Property on non-error
- 1.2.8 Request Response Information = 1 → CONNACK includes Response Information property

### 1.3 CONNACK — Server Capabilities
- 1.3.1 CONNACK contains Receive Maximum property
- 1.3.2 CONNACK contains Maximum QoS property
- 1.3.3 CONNACK contains Retain Available property
- 1.3.4 CONNACK contains Maximum Packet Size property
- 1.3.5 CONNACK contains Topic Alias Maximum property
- 1.3.6 CONNACK contains Wildcard Subscription Available property
- 1.3.7 CONNACK contains Subscription Identifier Available property
- 1.3.8 CONNACK contains Shared Subscription Available property

### 1.4 Clean Start
- 1.4.1 Clean Start = 1 → new session, Session Present = 0
- 1.4.2 Clean Start = 0, no prior session → new session, Session Present = 0
- 1.4.3 Clean Start = 0, prior session exists → resume session, Session Present = 1
- 1.4.4 Clean Start = 1, prior session exists → discard old session, Session Present = 0

### 1.5 Session Takeover
- 1.5.1 Second client connects with same Client ID → first client disconnected with 0x8E (Session taken over)
- 1.5.2 Session state transferred to new connection
- 1.5.3 Old connection's subscriptions remain active after takeover

### 1.6 DISCONNECT
- 1.6.1 Client sends DISCONNECT with Reason 0x00 → clean close, will NOT published
- 1.6.2 Client sends DISCONNECT with Reason 0x04 → will IS published
- 1.6.3 Client DISCONNECT with Session Expiry Interval override → new expiry used
- 1.6.4 Client DISCONNECT cannot increase Session Expiry from 0 to non-zero (Protocol Error)
- 1.6.5 Server-initiated DISCONNECT with Reason Code and Reason String

### 1.7 Keep Alive
- 1.7.1 Client sends PINGREQ → broker responds PINGRESP
- 1.7.2 Client goes silent beyond 1.5× Keep Alive → broker closes connection
- 1.7.3 Keep Alive = 0 → no timeout enforced
- 1.7.4 Server Keep Alive override in CONNACK → client must use server's value

### 1.8 Connection Errors
- 1.8.1 First packet is not CONNECT → connection closed
- 1.8.2 Second CONNECT packet on same connection → Protocol Error 0x82
- 1.8.3 Malformed packet → DISCONNECT 0x81, connection closed
- 1.8.4 Abrupt TCP close → broker detects connection loss

---

## 2. Publish & Subscribe — Core

### 2.1 QoS 0 — At Most Once
- 2.1.1 Publish QoS 0 → subscriber receives message
- 2.1.2 Publish QoS 0 → no ACK packets exchanged
- 2.1.3 Publish QoS 0 to topic with no subscribers → no error, message dropped

### 2.2 QoS 1 — At Least Once
- 2.2.1 Publish QoS 1 → broker sends PUBACK to publisher
- 2.2.2 Publish QoS 1 → subscriber receives message with QoS ≤ subscribed QoS
- 2.2.3 Publisher does not receive PUBACK within timeout → retransmit with DUP=1
- 2.2.4 Broker sends PUBACK with Reason Code 0x00 (Success)
- 2.2.5 Broker sends PUBACK with Reason Code 0x10 (No matching subscribers) when applicable
- 2.2.6 Publish QoS 1 not authorized → PUBACK with 0x87

### 2.3 QoS 2 — Exactly Once
- 2.3.1 Full QoS 2 flow: PUBLISH → PUBREC → PUBREL → PUBCOMP
- 2.3.2 Subscriber receives message exactly once
- 2.3.3 Duplicate PUBLISH (same Packet ID) → PUBREC again, message NOT delivered twice
- 2.3.4 PUBREC with error reason → flow aborted, no PUBREL
- 2.3.5 PUBREL retransmission after PUBREC
- 2.3.6 PUBCOMP completes the flow, Packet ID released
- 2.3.7 Publish QoS 2 not authorized → PUBREC with 0x87

### 2.4 QoS Downgrade
- 2.4.1 Publish QoS 2 to subscriber with max QoS 1 → delivered as QoS 1
- 2.4.2 Publish QoS 2 to subscriber with max QoS 0 → delivered as QoS 0
- 2.4.3 Publish QoS 1 to subscriber with max QoS 0 → delivered as QoS 0
- 2.4.4 Server Maximum QoS in CONNACK respected by broker's outbound delivery

### 2.5 SUBSCRIBE
- 2.5.1 Subscribe to single topic filter → SUBACK with granted QoS
- 2.5.2 Subscribe to multiple topic filters in single SUBSCRIBE → SUBACK per filter
- 2.5.3 Subscribe to invalid topic filter → SUBACK with error reason code
- 2.5.4 Subscribe updates existing subscription (same filter, different QoS) → new QoS active
- 2.5.5 SUBACK Packet ID matches SUBSCRIBE Packet ID
- 2.5.6 Subscribe not authorized → SUBACK with 0x87 per denied filter

### 2.6 UNSUBSCRIBE
- 2.6.1 Unsubscribe from active subscription → no more messages delivered
- 2.6.2 Unsubscribe from non-existent subscription → UNSUBACK with 0x11 (No subscription existed)
- 2.6.3 UNSUBACK Packet ID matches UNSUBSCRIBE Packet ID
- 2.6.4 Unsubscribe from multiple filters in single packet → per-filter reason codes

---

## 3. Topic Matching

### 3.1 Exact Topic Match
- 3.1.1 Subscribe "a/b/c", publish "a/b/c" → message delivered
- 3.1.2 Subscribe "a/b/c", publish "a/b/d" → message NOT delivered
- 3.1.3 Subscribe "a/b/c", publish "a/b" → message NOT delivered
- 3.1.4 Subscribe "a/b/c", publish "a/b/c/d" → message NOT delivered

### 3.2 Single-Level Wildcard (+)
- 3.2.1 Subscribe "a/+/c", publish "a/b/c" → message delivered
- 3.2.2 Subscribe "a/+/c", publish "a/x/c" → message delivered
- 3.2.3 Subscribe "a/+/c", publish "a/b/d" → message NOT delivered
- 3.2.4 Subscribe "+/b/c", publish "a/b/c" → message delivered
- 3.2.5 Subscribe "a/+", publish "a/b" → message delivered
- 3.2.6 Subscribe "+", publish "a" → message delivered
- 3.2.7 Subscribe "a/+/c", publish "a/b/c/d" → message NOT delivered

### 3.3 Multi-Level Wildcard (#)
- 3.3.1 Subscribe "a/#", publish "a/b" → message delivered
- 3.3.2 Subscribe "a/#", publish "a/b/c/d" → message delivered
- 3.3.3 Subscribe "a/#", publish "a" → message delivered
- 3.3.4 Subscribe "#", publish any topic → message delivered
- 3.3.5 Subscribe "#", publish to $SYS topic → message NOT delivered (system topic exclusion)

### 3.4 System Topics ($SYS)
- 3.4.1 Subscribe "$SYS/#" → receives $SYS messages
- 3.4.2 Subscribe "#" → does NOT receive $SYS messages
- 3.4.3 Subscribe "+/broker/uptime" → does NOT match $SYS/broker/uptime
- 3.4.4 Client cannot publish to $SYS topics (reserved for broker)

### 3.5 Combined Wildcards
- 3.5.1 Subscribe "sport/+/player/#" → matches "sport/tennis/player/ranking"
- 3.5.2 Subscribe "+/+/+" → matches "a/b/c" but not "a/b/c/d"
- 3.5.3 Multiple overlapping subscriptions → message delivered once per subscription (with Subscription Identifier)

---

## 4. Retained Messages

### 4.1 Store and Deliver
- 4.1.1 Publish with RETAIN=1 → message stored
- 4.1.2 New subscriber to that topic → receives retained message immediately
- 4.1.3 Retained delivery with default subscription options clears RETAIN flag (RAP=0)
- 4.1.4 Publish new retained message to same topic → old message replaced

### 4.2 Delete Retained Message
- 4.2.1 Publish with RETAIN=1 and empty payload → retained message deleted
- 4.2.2 New subscriber after deletion → no retained message delivered

### 4.3 Retain Handling Options
- 4.3.1 Retain Handling = 0 → send retained messages on every subscribe
- 4.3.2 Retain Handling = 1 → send retained messages only on new subscribe (not re-subscribe)
- 4.3.3 Retain Handling = 2 → never send retained messages on subscribe

### 4.4 Retain As Published
- 4.4.1 Subscription with Retain As Published = 1 → RETAIN flag preserved in forwarded PUBLISH
- 4.4.2 Subscription with Retain As Published = 0 → RETAIN flag cleared in forwarded PUBLISH

### 4.5 Retained Message Expiry
- 4.5.1 Retained message with Message Expiry Interval → message removed after expiry
- 4.5.2 New subscriber after expiry → no retained message delivered

---

## 5. Will Messages

### 5.1 Will Trigger — Connection Loss
- 5.1.1 Client with will disconnects abruptly (TCP close) → will message published
- 5.1.2 Client with will keeps alive expired → will message published
- 5.1.3 Client with will sends normal DISCONNECT (0x00) → will NOT published
- 5.1.4 Client with will sends DISCONNECT with 0x04 → will IS published

### 5.2 Will Delay Interval
- 5.2.1 Will Delay = 0 → will published immediately on connection loss
- 5.2.2 Will Delay > 0 → will published after delay
- 5.2.3 Client reconnects before Will Delay expires → will NOT published
- 5.2.4 Session expires before Will Delay → will published immediately at session expiry

### 5.3 Will Properties
- 5.3.1 Will with QoS 0 → will delivered at QoS 0
- 5.3.2 Will with QoS 1 → will delivered at QoS 1 with proper ACK flow
- 5.3.3 Will with QoS 2 → will delivered at QoS 2 with proper handshake
- 5.3.4 Will with RETAIN=1 → will stored as retained message
- 5.3.5 Will with Message Expiry Interval → will expires if not delivered in time
- 5.3.6 Will with Payload Format Indicator and Content Type → properties forwarded
- 5.3.7 Will with User Properties → properties forwarded to subscribers
- 5.3.8 Will with Response Topic and Correlation Data → properties forwarded

---

## 6. Session Persistence

### 6.1 Session Resume
- 6.1.1 Disconnect (Session Expiry > 0), reconnect Clean Start = 0 → Session Present = 1
- 6.1.2 Subscriptions survive across reconnect
- 6.1.3 Offline queued messages delivered after reconnect
- 6.1.4 QoS 1 inflight messages retransmitted after reconnect (DUP=1)
- 6.1.5 QoS 2 inflight state resumed after reconnect

### 6.2 Session Expiry
- 6.2.1 Session Expiry Interval = 0 → session deleted on disconnect
- 6.2.2 Session Expiry Interval elapsed → session deleted, subscriptions removed
- 6.2.3 Reconnect after session expired → Session Present = 0, fresh session
- 6.2.4 Session Expiry Interval override on DISCONNECT → new interval used

### 6.3 Offline Message Queue
- 6.3.1 QoS 1 message published while subscriber offline → queued, delivered on reconnect
- 6.3.2 QoS 2 message published while subscriber offline → queued, delivered on reconnect
- 6.3.3 QoS 0 messages NOT queued for offline sessions
- 6.3.4 Queue size limit enforced → oldest messages dropped when full
- 6.3.5 Message Expiry applied → expired messages not delivered on reconnect

---

## 7. Subscription Options

### 7.1 No Local
- 7.1.1 Subscribe with No Local = 1, publish to same topic from same client → message NOT received
- 7.1.2 Subscribe with No Local = 0, publish to same topic from same client → message received
- 7.1.3 Subscribe with No Local = 1, other client publishes → message received

### 7.2 Subscription Identifier
- 7.2.1 Subscribe with Subscription Identifier → outbound PUBLISH includes Subscription Identifier property
- 7.2.2 Multiple subscriptions with different Identifiers matching same topic → both Identifiers in PUBLISH
- 7.2.3 Subscription Identifier = 0 → Protocol Error

### 7.3 Shared Subscriptions
- 7.3.1 $share/group/topic → message delivered to exactly one group member
- 7.3.2 Multiple group members → messages distributed (round-robin or load-balanced)
- 7.3.3 Group member disconnects → remaining members receive messages
- 7.3.4 All group members disconnect → group dissolved
- 7.3.5 Non-shared subscriber on same topic receives its own copy
- 7.3.6 No Local not supported on shared subscriptions → Protocol Error

---

## 8. Topic Alias

### 8.1 Client → Broker (Inbound)
- 8.1.1 Client sends PUBLISH with Topic + Alias → broker creates mapping
- 8.1.2 Client sends PUBLISH with Alias only (empty topic) → broker resolves from stored mapping
- 8.1.3 Client sends Alias > Topic Alias Maximum → Protocol Error
- 8.1.4 Client sends Alias without prior mapping and empty topic → Protocol Error

### 8.2 Broker → Client (Outbound)
- 8.2.1 Broker uses Topic Alias when client's Topic Alias Maximum > 0
- 8.2.2 Broker does not exceed client's Topic Alias Maximum
- 8.2.3 Client with Topic Alias Maximum = 0 → broker never sends Topic Alias

---

## 9. Flow Control

### 9.1 Receive Maximum
- 9.1.1 Broker does not send more unacknowledged QoS 1/2 PUBLISH than client's Receive Maximum
- 9.1.2 Client exceeds broker's Receive Maximum → Protocol Error 0x93
- 9.1.3 After ACK received, broker resumes sending (slot freed)

### 9.2 Maximum Packet Size
- 9.2.1 Client sets Maximum Packet Size → broker never sends packet exceeding it
- 9.2.2 Client sends packet exceeding broker's Maximum Packet Size → DISCONNECT 0x95
- 9.2.3 Message too large for subscriber's limit → message silently dropped for that subscriber

---

## 10. Authentication

### 10.1 Username/Password
- 10.1.1 Valid credentials → CONNACK success
- 10.1.2 Invalid credentials → CONNACK 0x86
- 10.1.3 Missing required credentials → CONNACK 0x86

### 10.2 Enhanced Authentication (AUTH Packet)
- 10.2.1 CONNECT with Authentication Method → challenge-response via AUTH packets
- 10.2.2 Successful multi-step handshake → CONNACK success
- 10.2.3 Failed handshake → CONNACK 0x86
- 10.2.4 Unknown Authentication Method → CONNACK 0x8C
- 10.2.5 Re-authentication during active session (AUTH Reason 0x19) → success
- 10.2.6 Re-authentication failure → DISCONNECT

### 10.3 Anonymous Access
- 10.3.1 Anonymous access enabled → connect without credentials succeeds
- 10.3.2 Anonymous access disabled → connect without credentials fails

---

## 11. Authorization (ACL)

### 11.1 Publish Authorization
- 11.1.1 Client publishes to allowed topic → message routed
- 11.1.2 Client publishes to denied topic → PUBACK/PUBREC with 0x87 (or DISCONNECT for QoS 0)
- 11.1.3 ACL with wildcard pattern → matches correctly

### 11.2 Subscribe Authorization
- 11.2.1 Client subscribes to allowed topic filter → SUBACK with granted QoS
- 11.2.2 Client subscribes to denied topic filter → SUBACK with 0x87
- 11.2.3 Multiple filters in one SUBSCRIBE, mixed permissions → mixed SUBACK reason codes

---

## 12. Message Properties (End-to-End)

### 12.1 Payload Format Indicator
- 12.1.1 Publish with Payload Format Indicator = 1 (UTF-8) → forwarded to subscriber
- 12.1.2 Invalid UTF-8 payload with Format Indicator = 1 → broker may disconnect

### 12.2 Content Type
- 12.2.1 Publish with Content Type → forwarded to subscriber

### 12.3 Response Topic & Correlation Data
- 12.3.1 Publish with Response Topic → forwarded to subscriber
- 12.3.2 Publish with Correlation Data → forwarded to subscriber
- 12.3.3 Request/Response pattern: requester publishes with Response Topic, responder publishes to it

### 12.4 Message Expiry Interval
- 12.4.1 Message with Expiry Interval → value decremented in forwarded PUBLISH
- 12.4.2 Message expired before delivery → message dropped
- 12.4.3 Offline queue: expired messages removed before delivery on reconnect

### 12.5 User Properties
- 12.5.1 Publish with User Properties → forwarded to subscriber
- 12.5.2 Multiple User Properties → all forwarded

---

## 13. Error Handling & Protocol Conformance

### 13.1 Malformed Packet Handling
- 13.1.1 Truncated packet → DISCONNECT 0x81
- 13.1.2 Invalid remaining length encoding → DISCONNECT 0x81
- 13.1.3 Reserved bits set in Fixed Header → DISCONNECT 0x81
- 13.1.4 Invalid UTF-8 in topic → DISCONNECT 0x81
- 13.1.5 Unknown property ID → DISCONNECT 0x81
- 13.1.6 Duplicate non-repeatable property → DISCONNECT 0x82

### 13.2 Protocol Errors
- 13.2.1 Packet type not in Connected state → DISCONNECT 0x82
- 13.2.2 SUBSCRIBE with empty filter list → DISCONNECT 0x82
- 13.2.3 PUBLISH QoS 3 (invalid) → connection closed
- 13.2.4 PUBREL for unknown Packet ID → Reason Code 0x92 (Packet Identifier not found)

### 13.3 Reason Code Coverage
- 13.3.1 0x00 — Success in all ACK packet types
- 13.3.2 0x10 — No matching subscribers (PUBACK)
- 13.3.3 Malformed packet uses specific reason code 0x81 (must not fall back to 0x80)
- 13.3.4 0x81 — Malformed Packet
- 13.3.5 0x82 — Protocol Error
- 13.3.6 Authorized QoS1 publish to subscribed topic returns 0x00 (must not return 0x83)
- 13.3.7 0x86 — Bad User Name or Password
- 13.3.8 0x87 — Not Authorized
- 13.3.9 0x8B — Server shutting down (graceful shutdown)
- 13.3.10 0x8D — Keep Alive timeout
- 13.3.11 0x8E — Session taken over
- 13.3.12 0x91 — Packet Identifier in use
- 13.3.13 0x92 — Packet Identifier not found
- 13.3.14 0x93 — Receive Maximum exceeded
- 13.3.15 0x95 — Packet too large
- 13.3.16 0x97 — Quota exceeded
- 13.3.17 0x9E — Shared Subscriptions not supported (if disabled)

---

## 14. WebSocket Transport

### 14.1 WebSocket Handshake
- 14.1.1 HTTP Upgrade with correct headers → WebSocket connection established
- 14.1.2 Missing Sec-WebSocket-Protocol: mqtt → rejected
- 14.1.3 Invalid upgrade request → HTTP 400

### 14.2 MQTT over WebSocket
- 14.2.1 Full MQTT session over WebSocket (connect, pub, sub, disconnect)
- 14.2.2 QoS 1 end-to-end over WebSocket
- 14.2.3 QoS 2 end-to-end over WebSocket
- 14.2.4 Large payload over WebSocket (fragmented frames)
- 14.2.5 Binary and text frame handling

---

## 15. Monitoring ($SYS Topics)

### 15.1 Statistics Topics
- 15.1.1 $SYS/broker/clients/connected reflects actual connected clients
- 15.1.2 $SYS/broker/messages/received increments on inbound PUBLISH
- 15.1.3 $SYS/broker/messages/sent increments on outbound PUBLISH
- 15.1.4 $SYS/broker/subscriptions/count reflects active subscriptions
- 15.1.5 $SYS/broker/retained messages/count reflects stored retained messages
- 15.1.6 $SYS/broker/uptime increases over time

### 15.2 $SYS Subscription Behavior
- 15.2.1 Subscribe $SYS/# → receives periodic updates
- 15.2.2 Subscribe # → does NOT receive $SYS topics

---

## 16. Graceful Shutdown

### 16.1 Ordered Shutdown
- 16.1.1 SIGTERM → broker sends DISCONNECT 0x8B to all clients
- 16.1.2 All connections closed cleanly before process exits
- 16.1.3 Persistence flushed before exit (sessions, retained messages)
- 16.1.4 SIGINT → same behavior as SIGTERM

---

## 17. Multi-Client Scenarios

### 17.1 Fan-Out
- 17.1.1 1 publisher, 10 subscribers on same topic → all 10 receive message
- 17.1.2 1 publisher, subscribers with mixed QoS → each received at subscribed QoS

### 17.2 Fan-In
- 17.2.1 10 publishers to same topic, 1 subscriber → subscriber receives all 10 messages

### 17.3 Cross-Traffic
- 17.3.1 Multiple publishers and subscribers on different topics → correct routing, no cross-contamination
- 17.3.2 Client both publisher and subscriber on same topic → receives own messages (unless No Local)

### 17.4 Rapid Connect/Disconnect
- 17.4.1 Client connects and disconnects rapidly 100 times → broker stable, no leaks
- 17.4.2 Multiple clients connect simultaneously → all handled correctly

---

## 18. Load Tests

### 18.1 Connection Load
- 18.1.1 Fast baseline: 10 concurrent connections → all CONNACK success
- 18.1.2 Combined progressive connection-load test (covers former 18.1.1 + 18.1.2 + 18.1.3):
	- execute load in relative steps (doubling): 100 → 200 → 400 → 800 → ... → 12800
	- connection phase is load-oriented only (no connection-storm behavior)
	- open connections in small batches of 5 with short pauses between batches
	- keep reached connection level briefly stable before advancing to the next batch/stage
	- hard timeout is mandatory for the entire test
	- stop immediately at first failed stage (no check of n+1 after n fails)
	- report highest successful stage when a step fails or timeout is reached
	- success threshold is stage 800; higher stages are informational only
- 18.1.3 Connection storm: 100 clients connect within 1 second → no crash, all handled

### 18.2 Message Throughput
- 18.2.1 1 publisher, 1 subscriber, 1000 messages QoS 0 → all delivered, < 5s
- 18.2.2 1 publisher, 1 subscriber, 1000 messages QoS 1 → all ACKed and delivered
- 18.2.3 10 publishers, 10 subscribers (fan-out), 100 messages each → 10,000 deliveries total
- 18.2.4 1 publisher, 100 subscribers, 100 messages → 10,000 deliveries total
- 18.2.5 Large payload (256 KB) QoS 0 with INI `broker.write_queue_max_bytes` above frame size → delivered correctly
- 18.2.6 Large payload (256 KB) QoS 1 with INI `broker.write_queue_max_bytes` above frame size → delivered and ACKed
- 18.2.7 Large payload (256 KB) QoS 1 with INI `broker.write_queue_max_bytes` below frame size → PUBACK QuotaExceeded (0x97), no delivery to subscriber

### 18.3 Subscription Load
- 18.3.1 1 client subscribes to 1000 different topic filters → all active
- 18.3.2 100 clients each subscribe to 10 filters → routing correct for all
- 18.3.3 Wildcard subscription with 1000 matching topics → correct fan-out

### 18.4 Sustained Load
- 18.4.1 50 clients, continuous publish/subscribe for 60 seconds → no crash, no memory growth
- 18.4.2 Retained message store with 1000 entries → subscribe returns correct retained messages
- 18.4.3 Offline queue with 500 queued messages → all delivered on reconnect

---

## 19. Robustness Tests

### 19.1 Malformed Input
- 19.1.1 Send random garbage bytes → broker closes connection, no crash
- 19.1.2 Send truncated CONNECT packet → broker closes connection cleanly
- 19.1.3 Send oversized packet (> 256 MB) → broker rejects, no OOM
- 19.1.4 Send zero-length packet → broker handles gracefully
- 19.1.5 Send valid fixed header with wrong remaining length → broker detects and disconnects
- 19.1.6 Send PUBLISH with QoS 3 (reserved) → broker closes connection

### 19.2 Connection Abuse
- 19.2.1 Client opens TCP connection, sends nothing for 30s → broker closes (keep-alive or idle timeout)
- 19.2.2 Client opens TCP connection, sends partial CONNECT, stops → timeout, clean close
- 19.2.3 Client sends CONNECT, then flood of invalid packets → broker disconnects, stable
- 19.2.4 Client sends CONNECT, immediately TCP RST → broker cleans up, no resource leak
- 19.2.5 100 half-open connections (SYN but no data) → broker handles without deadlock

### 19.3 Resource Exhaustion
- 19.3.1 Client subscribes to extremely deep topic (100 levels) → handled or rejected, no crash
- 19.3.2 Client sends topic name at maximum length (65535 bytes) → handled correctly
- 19.3.3 Client sends maximum number of properties → parsed correctly
- 19.3.4 Packet Identifier exhaustion (65535 inflight QoS 1 messages) → broker handles gracefully
- 19.3.5 Rapid topic alias creation up to limit → all tracked, limit enforced

### 19.4 Recovery
- 19.4.1 Broker restart → retained messages restored from persistence
- 19.4.2 Broker restart → sessions with expiry restored
- 19.4.3 Broker restart → inflight QoS 1/2 messages resumed
- 19.4.4 Broker crash-recovery → data integrity maintained

### 19.5 Concurrency
- 19.5.1 Two clients publish to same topic simultaneously → both messages routed, no corruption
- 19.5.2 Client publishes while another subscribes to same topic → no race condition
- 19.5.3 Session takeover during active publish → no crash, messages not lost
- 19.5.4 100 clients connect/disconnect/publish/subscribe randomly for 30s → broker stable
- 19.5.5 Unsubscribe during message delivery → no crash, delivery consistent

---

## 20. Interoperability

### 20.1 Client Library Compatibility
- 20.1.1 MQTTX CLI client — full session lifecycle
- 20.1.2 Paho MQTT Python client — full session lifecycle
- 20.1.3 Mosquitto client tools (mosquitto_pub / mosquitto_sub) — basic pub/sub

### 20.2 Edge Cases from Spec
- 20.2.1 Empty topic filter in SUBSCRIBE → Protocol Error
- 20.2.2 UTF-8 topic names with multi-byte characters → delivered correctly
- 20.2.3 Null character (U+0000) in topic → rejected
- 20.2.4 Zero-length Client ID with Clean Start = 0 → rejected (MQTT 5.0 §3.1.3.1)
- 20.2.5 Payload with zero length → delivered correctly (valid empty payload)
- 20.2.6 PUBLISH to "/" (single-separator topic) → valid, delivered to subscribers of "/"
- 20.2.7 Subscribe to "/" → receives messages published to "/"
