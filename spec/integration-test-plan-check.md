# Integration Test Plan — Conformance Check

Scope: sections 0–13. Each test case is rated:
- **✓** — correctly implemented, protocol-conformant
- **⚠** — implemented but has a conformance or coverage gap
- **✗** — missing (no implementation found)

---

## 0. Prerequisites — Test Toolbox

### 0.1 MQTT Client Helper (`mqtt_client.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 0.1.1 | `connect()` → returns CONNACK result | ✓ | |
| 0.1.2 | `disconnect()` → sends DISCONNECT, waits for clean close | ✓ | |
| 0.1.3 | `publish()` → waits for ACK, returns reason code | ✓ | |
| 0.1.4 | `subscribe()` → returns SUBACK reason codes | ✓ | |
| 0.1.5 | `unsubscribe()` → returns UNSUBACK reason codes | ✓ | |
| 0.1.6 | `collect_messages()` → returns messages with properties | ✓ | |
| 0.1.7 | `wait_for_disconnect()` → returns reason code + properties | ✓ | Includes paho patch for DISCONNECT with remaining_length=2 |
| 0.1.8 | Will message configuration before connect | ✓ | |
| 0.1.9 | Topic Alias support (outbound alias table) | ✓ | `enable_topic_alias()` method |
| 0.1.10 | Context manager auto-disconnect | ✓ | |

### 0.2 Raw TCP Helper (`raw_tcp.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 0.2.1 | `send_bytes()` | ✓ | |
| 0.2.2 | `send_partial_connect()` | ✓ | |
| 0.2.3 | `open_idle_connection()` | ✓ | |
| 0.2.4 | `send_and_expect_close()` | ✓ | |
| 0.2.5 | `flood_connections()` | ✓ | |
| 0.2.6 | CONNECT packet builder | ✓ | `build_connect_packet()` with configurable flags/properties |
| 0.2.7 | PUBLISH packet builder | ✓ | `build_publish_packet()` with QoS, DUP, flags, properties |
| 0.2.8 | Generic packet builder | ✓ | `build_packet()` for arbitrary packet types; also `build_subscribe_packet()` / `build_unsubscribe_packet()` |

### 0.3 Assertions Helper (`assertions.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 0.3.1 | `assert_connack()` | ✓ | |
| 0.3.2 | `assert_connack_property()` | ✓ | |
| 0.3.3 | `assert_message()` | ✓ | |
| 0.3.4 | `assert_message_property()` | ✓ | |
| 0.3.5 | `assert_reason_code()` | ✓ | |
| 0.3.6 | `assert_disconnected()` | ✓ | |
| 0.3.7 | `assert_no_message()` | ✓ | |
| 0.3.8 | `assert_connection_closed()` | ✓ | |

### 0.4 Broker Lifecycle Helper (`broker.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 0.4.1 | `start_broker()` | ✓ | Config overrides via temp INI file; waits for reachability |
| 0.4.2 | `stop_broker()` | ✓ | |
| 0.4.3 | `restart_broker()` | ✓ | |
| 0.4.4 | `is_reachable()` | ✓ | |

### 0.5 Smoke Tests (`toolbox_smoke.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 0.5.1 | paho-mqtt library available | ✓ | |
| 0.5.2 | MqttClient connect/disconnect | ✓ | |
| 0.5.3 | QoS 0 roundtrip | ✓ | |
| 0.5.4 | QoS 1 roundtrip | ✓ | |
| 0.5.5 | QoS 2 roundtrip | ✓ | |
| 0.5.6 | Raw TCP send/receive | ✓ | |
| 0.5.7 | Assertions produce clear error messages | ✓ | |

---

## 1. Connection Lifecycle

### 1.1 CONNECT — Basic (`connect_basic.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 1.1.1 | Anonymous connect → CONNACK success | ✓ | |
| 1.1.2 | Valid username/password → CONNACK success | ✓ | |
| 1.1.3 | Invalid credentials → CONNACK 0x86 | ✓ | |
| 1.1.4 | Empty Client ID → broker assigns ID (Assigned Client Identifier) | ✓ | |
| 1.1.5 | Explicit Client ID preserved | ✓ | |
| 1.1.6 | Invalid protocol version → connection refused | ✓ | |
| 1.1.7 | Invalid protocol name → connection closed | ✓ | |
| 1.1.8 | Reserved header flags set → connection closed | ✓ | Sends CONNECT with flags byte `0x01` (reserved bit set) |

### 1.2 CONNECT — Properties (`connect_properties.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 1.2.1 | Session Expiry = 0 → session discarded on disconnect | ✓ | |
| 1.2.2 | Session Expiry > 0 → session persisted | ✓ | |
| 1.2.3 | Session Expiry = 0xFFFFFFFF → session never expires | ✓ | |
| 1.2.4 | Receive Maximum → broker respects client-side inflight limit | ⚠ | Verifies QoS1 delivery succeeds with ReceiveMaximum=1, but does **not** verify the broker actively withholds a second unacknowledged PUBLISH until the first is ACKed. Throttling enforcement is untested. |
| 1.2.5 | Maximum Packet Size → broker never sends oversized packets | ✓ | |
| 1.2.6 | Topic Alias Maximum → broker respects alias limit | ✓ | |
| 1.2.7 | Request Problem Information = 0 → broker omits Reason String / User Property | ✓ | |
| 1.2.8 | Request Response Information = 1 → CONNACK includes Response Information | ✓ | |

### 1.3 CONNACK — Server Capabilities (`connack_server_capabilities.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 1.3.1 | CONNACK contains Receive Maximum | ✓ | Presence verified; value not asserted (acceptable for capability advertisement) |
| 1.3.2 | CONNACK contains Maximum QoS | ✓ | |
| 1.3.3 | CONNACK contains Retain Available | ✓ | |
| 1.3.4 | CONNACK contains Maximum Packet Size | ✓ | |
| 1.3.5 | CONNACK contains Topic Alias Maximum | ✓ | |
| 1.3.6 | CONNACK contains Wildcard Subscription Available | ✓ | |
| 1.3.7 | CONNACK contains Subscription Identifier Available | ✓ | |
| 1.3.8 | CONNACK contains Shared Subscription Available | ✓ | |

### 1.4 Clean Start (`clean_start.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 1.4.1 | Clean Start = 1 → Session Present = 0 | ✓ | |
| 1.4.2 | Clean Start = 0, no prior session → Session Present = 0 | ✓ | |
| 1.4.3 | Clean Start = 0, prior session → Session Present = 1 | ✓ | |
| 1.4.4 | Clean Start = 1, prior session → discard, Session Present = 0 | ✓ | |

### 1.5 Session Takeover (`session_takeover.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 1.5.1 | Second client with same ID → first gets DISCONNECT 0x8E | ✓ | |
| 1.5.2 | Session state transferred to new connection | ✓ | |
| 1.5.3 | Old connection's subscriptions remain after takeover | ✓ | |

### 1.6 DISCONNECT (`disconnect.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 1.6.1 | DISCONNECT 0x00 → will NOT published | ✓ | |
| 1.6.2 | DISCONNECT 0x04 → will IS published | ✓ | |
| 1.6.3 | DISCONNECT with Session Expiry override | ✓ | |
| 1.6.4 | DISCONNECT cannot increase Session Expiry from 0 → Protocol Error | ✓ | Uses raw TCP to send DISCONNECT with Session Expiry property |
| 1.6.5 | Server-initiated DISCONNECT with Reason Code + Reason String | ✓ | |

### 1.7 Keep Alive (`keep_alive.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 1.7.1 | PINGREQ → PINGRESP | ✓ | |
| 1.7.2 | Silent beyond 1.5× Keep Alive → broker closes | ✓ | |
| 1.7.3 | Keep Alive = 0 → no timeout enforced | ✓ | Uses `time.sleep(2.2)` to verify connection survives |
| 1.7.4 | Server Keep Alive override in CONNACK → client uses server's value | ✓ | |

### 1.8 Connection Errors (`connection_errors.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 1.8.1 | First packet not CONNECT → connection closed | ✓ | |
| 1.8.2 | Second CONNECT on same connection → Protocol Error 0x82 | ✓ | |
| 1.8.3 | Malformed packet → DISCONNECT 0x81 | ✓ | |
| 1.8.4 | Abrupt TCP close → broker detects connection loss | ⚠ | Detection is inferred via will message publication after keepalive=1 timeout, not via direct TCP RST/abrupt-close detection. The test verifies the *effect* (will published) but couples it to the keepalive timeout mechanism rather than isolating pure abrupt-close detection. |

---

## 2. Publish & Subscribe — Core

### 2.1 QoS 0 (`core_qos.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 2.1.1 | Publish QoS 0 → subscriber receives | ✓ | |
| 2.1.2 | Publish QoS 0 → no ACK packets exchanged | ✓ | |
| 2.1.3 | Publish QoS 0, no subscribers → no error | ✓ | |

### 2.2 QoS 1 (`core_qos.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 2.2.1 | Publish QoS 1 → broker sends PUBACK | ✓ | |
| 2.2.2 | Publish QoS 1 → subscriber receives at QoS ≤ subscribed | ✓ | |
| 2.2.3 | Retransmit with DUP=1 → broker accepts, subscriber gets one message | ✓ | Uses raw TCP to send duplicate PUBLISH with DUP=1 |
| 2.2.4 | PUBACK with reason 0x00 | ✓ | |
| 2.2.5 | PUBACK 0x10 (No matching subscribers) | ✓ | |
| 2.2.6 | Publish QoS 1 not authorized → PUBACK 0x87 | ✓ | |

### 2.3 QoS 2 (`core_qos.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 2.3.1 | Full QoS 2 flow: PUBLISH → PUBREC → PUBREL → PUBCOMP | ✓ | |
| 2.3.2 | Subscriber receives exactly once | ✓ | |
| 2.3.3 | Duplicate PUBLISH (same Packet ID) → PUBREC again, no double delivery | ✓ | |
| 2.3.4 | PUBREC with error reason → flow aborted, no PUBREL | ✓ | |
| 2.3.5 | PUBREL retransmission after PUBREC | ✓ | |
| 2.3.6 | PUBCOMP completes flow, Packet ID released | ✓ | |
| 2.3.7 | Publish QoS 2 not authorized → PUBREC 0x87 | ✓ | |

### 2.4 QoS Downgrade (`core_qos.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 2.4.1 | Publish QoS 2, subscriber max QoS 1 → delivered as QoS 1 | ✓ | |
| 2.4.2 | Publish QoS 2, subscriber max QoS 0 → delivered as QoS 0 | ✓ | |
| 2.4.3 | Publish QoS 1, subscriber max QoS 0 → delivered as QoS 0 | ✓ | |
| 2.4.4 | Server Maximum QoS respected in outbound delivery | ✓ | |

### 2.5 SUBSCRIBE (`subscribe_unsubscribe.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 2.5.1 | Subscribe single filter → SUBACK with granted QoS | ✓ | |
| 2.5.2 | Subscribe multiple filters in one SUBSCRIBE → per-filter SUBACK | ✓ | |
| 2.5.3 | Subscribe invalid filter → SUBACK with error reason | ✓ | |
| 2.5.4 | Subscribe updates existing subscription (new QoS active) | ✓ | |
| 2.5.5 | SUBACK Packet ID matches SUBSCRIBE Packet ID | ✓ | Verified with raw TCP |
| 2.5.6 | Subscribe not authorized → SUBACK 0x87 | ✓ | |

### 2.6 UNSUBSCRIBE (`subscribe_unsubscribe.py`)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 2.6.1 | Unsubscribe → no more messages delivered | ✓ | |
| 2.6.2 | Unsubscribe non-existent → UNSUBACK 0x11 | ✓ | |
| 2.6.3 | UNSUBACK Packet ID matches UNSUBSCRIBE Packet ID | ✓ | Verified with raw TCP |
| 2.6.4 | Unsubscribe multiple filters → per-filter reason codes | ✓ | |

---

## 3. Topic Matching (`topic_matching.py`)

### 3.1 Exact Match

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 3.1.1 | Subscribe "a/b/c", publish "a/b/c" → delivered | ✓ | |
| 3.1.2 | Subscribe "a/b/c", publish "a/b/d" → NOT delivered | ✓ | |
| 3.1.3 | Subscribe "a/b/c", publish "a/b" → NOT delivered | ✓ | |
| 3.1.4 | Subscribe "a/b/c", publish "a/b/c/d" → NOT delivered | ✓ | |

### 3.2 Single-Level Wildcard (+)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 3.2.1 | "a/+/c" matches "a/b/c" | ✓ | |
| 3.2.2 | "a/+/c" matches "a/x/c" | ✓ | |
| 3.2.3 | "a/+/c" does not match "a/b/d" | ✓ | |
| 3.2.4 | "+/b/c" matches "a/b/c" | ✓ | |
| 3.2.5 | "a/+" matches "a/b" | ✓ | |
| 3.2.6 | "+" matches "a" | ✓ | |
| 3.2.7 | "a/+/c" does not match "a/b/c/d" | ✓ | |

### 3.3 Multi-Level Wildcard (#)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 3.3.1 | "a/#" matches "a/b" | ✓ | |
| 3.3.2 | "a/#" matches "a/b/c/d" | ✓ | |
| 3.3.3 | "a/#" matches "a" | ✓ | |
| 3.3.4 | "#" matches any topic | ✓ | |
| 3.3.5 | "#" does NOT match $SYS topics | ✓ | |

### 3.4 System Topics ($SYS)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 3.4.1 | Subscribe "$SYS/#" → receives $SYS messages | ✓ | |
| 3.4.2 | Subscribe "#" → does NOT receive $SYS messages | ✓ | |
| 3.4.3 | "+/broker/uptime" does NOT match "$SYS/broker/uptime" | ✓ | |
| 3.4.4 | Client cannot publish to $SYS topics | ✓ | |

### 3.5 Combined Wildcards

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 3.5.1 | "sport/+/player/#" matches "sport/tennis/player/ranking" | ✓ | |
| 3.5.2 | "+/+/+" matches "a/b/c" but not "a/b/c/d" | ✓ | |
| 3.5.3 | Overlapping subscriptions → message delivered once per subscription with Subscription Identifier | ✓ | |

---

## 4. Retained Messages (`retained_messages.py`)

### 4.1 Store and Deliver

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 4.1.1 | Publish RETAIN=1 → message stored | ✓ | |
| 4.1.2 | New subscriber receives retained message immediately | ✓ | |
| 4.1.3 | Retained message has RETAIN flag set in delivered PUBLISH | ⚠ | Test uses `retainAsPublished=True` subscription option to observe RETAIN=1 in the forwarded PUBLISH. Per MQTT 5.0 §3.3.1.3, the RETAIN flag is **cleared** by default (retainAsPublished=0) on forwarded publishes. The test effectively validates 4.4.1 (Retain As Published=1) behavior, not the default delivery behavior described by 4.1.3. The spec-conformant default (RETAIN cleared) is not verified separately. |
| 4.1.4 | New retained message replaces old | ✓ | |

### 4.2 Delete Retained Message

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 4.2.1 | Publish RETAIN=1 + empty payload → retained message deleted | ✓ | |
| 4.2.2 | New subscriber after deletion → no retained message | ✓ | |

### 4.3 Retain Handling Options

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 4.3.1 | Retain Handling = 0 → retained message on every subscribe | ✓ | |
| 4.3.2 | Retain Handling = 1 → retained message only on new subscribe | ✓ | |
| 4.3.3 | Retain Handling = 2 → never send retained message on subscribe | ✓ | |

### 4.4 Retain As Published

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 4.4.1 | Retain As Published = 1 → RETAIN flag preserved in forwarded PUBLISH | ✓ | |
| 4.4.2 | Retain As Published = 0 → RETAIN flag cleared in forwarded PUBLISH | ✓ | |

### 4.5 Retained Message Expiry

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 4.5.1 | Retained message with Expiry → removed after expiry | ✓ | |
| 4.5.2 | New subscriber after expiry → no retained message | ✓ | |

---

## 5. Will Messages (`will_messages.py`)

### 5.1 Will Trigger

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 5.1.1 | Abrupt TCP close → will published | ✓ | `_force_abrupt_close()` used |
| 5.1.2 | Keep-alive expired → will published | ✓ | |
| 5.1.3 | Normal DISCONNECT 0x00 → will NOT published | ✓ | |
| 5.1.4 | DISCONNECT 0x04 → will IS published | ✓ | |

### 5.2 Will Delay Interval

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 5.2.1 | Will Delay = 0 → will published immediately | ✓ | |
| 5.2.2 | Will Delay > 0 → will published after delay | ✓ | |
| 5.2.3 | Client reconnects before Will Delay → will NOT published | ✓ | |
| 5.2.4 | Session expires before Will Delay → will published at session expiry | ✓ | |

### 5.3 Will Properties

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 5.3.1 | Will QoS 0 → delivered at QoS 0 | ✓ | |
| 5.3.2 | Will QoS 1 → delivered at QoS 1 with ACK | ✓ | |
| 5.3.3 | Will QoS 2 → full handshake | ✓ | |
| 5.3.4 | Will RETAIN=1 → stored as retained | ✓ | |
| 5.3.5 | Will with Message Expiry Interval | ✓ | |
| 5.3.6 | Will with Payload Format Indicator + Content Type forwarded | ✓ | |
| 5.3.7 | Will with User Properties forwarded | ✓ | |
| 5.3.8 | Will with Response Topic + Correlation Data forwarded | ✓ | |

---

## 6. Session Persistence (`session_persistence.py`)

### 6.1 Session Resume

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 6.1.1 | Disconnect (Expiry > 0), reconnect Clean Start = 0 → Session Present = 1 | ✓ | |
| 6.1.2 | Subscriptions survive across reconnect | ✓ | |
| 6.1.3 | Offline queued messages delivered after reconnect | ✓ | |
| 6.1.4 | QoS 1 inflight retransmitted after reconnect (DUP=1) | ✓ | Verified at raw TCP packet level |
| 6.1.5 | QoS 2 inflight state resumed after reconnect | ✓ | Verified at raw TCP packet level |

### 6.2 Session Expiry

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 6.2.1 | Session Expiry = 0 → session deleted on disconnect | ✓ | |
| 6.2.2 | Session Expiry elapsed → session deleted | ✓ | |
| 6.2.3 | Reconnect after session expired → Session Present = 0 | ✓ | |
| 6.2.4 | Session Expiry override on DISCONNECT → new interval used | ✓ | |

### 6.3 Offline Message Queue

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 6.3.1 | QoS 1 queued while offline, delivered on reconnect | ✓ | |
| 6.3.2 | QoS 2 queued while offline, delivered on reconnect | ✓ | |
| 6.3.3 | QoS 0 NOT queued for offline sessions | ✓ | |
| 6.3.4 | Queue size limit enforced (oldest dropped when full) | ✓ | `broker.max_queued_messages=2` config override |
| 6.3.5 | Message Expiry applied → expired messages not delivered | ✓ | |

---

## 7. Subscription Options (`subscription_options.py`)

### 7.1 No Local

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 7.1.1 | No Local = 1, publisher is subscriber → NOT received | ✓ | |
| 7.1.2 | No Local = 0, publisher is subscriber → received | ✓ | |
| 7.1.3 | No Local = 1, other client publishes → received | ✓ | |

### 7.2 Subscription Identifier

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 7.2.1 | Subscribe with Subscription Identifier → outbound PUBLISH includes it | ✓ | |
| 7.2.2 | Multiple subscriptions with different IDs matching same topic → both IDs in PUBLISH | ✓ | |
| 7.2.3 | Subscription Identifier = 0 → Protocol Error | ✓ | |

### 7.3 Shared Subscriptions

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 7.3.1 | $share/group/topic → delivered to exactly one member | ✓ | |
| 7.3.2 | Multiple members → messages distributed | ✓ | |
| 7.3.3 | Group member disconnects → remaining members receive | ✓ | |
| 7.3.4 | All members disconnect → group dissolved | ✓ | |
| 7.3.5 | Non-shared subscriber on same topic gets own copy | ✓ | |
| 7.3.6 | No Local on shared subscription → Protocol Error | ✓ | |

---

## 8. Topic Alias (`topic_alias.py`)

### 8.1 Client → Broker (Inbound)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 8.1.1 | PUBLISH with Topic + Alias → broker creates mapping | ✓ | |
| 8.1.2 | PUBLISH with Alias only (empty topic) → broker resolves from mapping | ✓ | |
| 8.1.3 | Alias > Topic Alias Maximum → Protocol Error | ✓ | |
| 8.1.4 | Alias without prior mapping + empty topic → Protocol Error | ✓ | |

### 8.2 Broker → Client (Outbound)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 8.2.1 | Broker uses Topic Alias when client's Topic Alias Maximum > 0 | ✓ | |
| 8.2.2 | Broker does not exceed client's Topic Alias Maximum | ✓ | |
| 8.2.3 | Client Topic Alias Maximum = 0 → broker never sends alias | ✓ | |

---

## 9. Flow Control (`flow_control.py`)

### 9.1 Receive Maximum

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 9.1.1 | Broker holds back QoS 1/2 PUBLISH beyond client's Receive Maximum | ✓ | Raw TCP used; verifies second PUBLISH not sent before first ACKed |
| 9.1.2 | Client exceeds broker's Receive Maximum → Protocol Error 0x93 | ✓ | |
| 9.1.3 | After ACK, broker resumes (slot freed) | ✓ | |

### 9.2 Maximum Packet Size

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 9.2.1 | Client Maximum Packet Size → broker never exceeds it | ✓ | |
| 9.2.2 | Client sends packet exceeding broker's limit → DISCONNECT 0x95 | ✓ | |
| 9.2.3 | Message too large for subscriber → silently dropped for that subscriber | ✓ | |

---

## 10. Authentication (`authentication.py`)

### 10.1 Username/Password

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 10.1.1 | Valid credentials → CONNACK success | ✓ | |
| 10.1.2 | Invalid credentials → CONNACK 0x86 | ✓ | |
| 10.1.3 | Missing required credentials → CONNACK 0x86 | ✓ | |

### 10.2 Enhanced Authentication (AUTH Packet)

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 10.2.1 | CONNECT with Authentication Method → challenge via AUTH packets | ✓ | |
| 10.2.2 | Successful multi-step handshake → CONNACK success | ✓ | |
| 10.2.3 | Failed handshake → CONNACK 0x86 | ✓ | |
| 10.2.4 | Unknown Authentication Method → CONNACK 0x8C | ✓ | |
| 10.2.5 | Re-authentication during session (AUTH 0x19) → success | ✓ | |
| 10.2.6 | Re-authentication failure → DISCONNECT | ✓ | |

### 10.3 Anonymous Access

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 10.3.1 | Anonymous access enabled → connect without credentials succeeds | ✓ | |
| 10.3.2 | Anonymous access disabled → connect without credentials fails | ✓ | |

---

## 11. Authorization (`authorization.py`)

### 11.1 Publish Authorization

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 11.1.1 | Publish to allowed topic → message routed | ✓ | |
| 11.1.2 | Publish to denied topic → PUBACK 0x87 | ✓ | |
| 11.1.3 | ACL wildcard pattern matches correctly | ✓ | |

### 11.2 Subscribe Authorization

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 11.2.1 | Subscribe allowed filter → SUBACK with granted QoS | ✓ | |
| 11.2.2 | Subscribe denied filter → SUBACK 0x87 | ✓ | |
| 11.2.3 | Mixed permissions in one SUBSCRIBE → mixed SUBACK reason codes | ✓ | |

---

## 12. Message Properties (`message_properties.py`)

### 12.1 Payload Format Indicator

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 12.1.1 | Payload Format Indicator = 1 forwarded to subscriber | ✓ | |
| 12.1.2 | Invalid UTF-8 with Format Indicator = 1 → broker may disconnect | ✓ | Test accepts both outcomes (broker disconnects or continues); the spec says "MAY" so this is acceptable |

### 12.2 Content Type

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 12.2.1 | Content Type forwarded | ✓ | |

### 12.3 Response Topic & Correlation Data

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 12.3.1 | Response Topic forwarded | ✓ | |
| 12.3.2 | Correlation Data forwarded | ✓ | |
| 12.3.3 | Request/Response pattern with Response Topic | ✓ | |

### 12.4 Message Expiry Interval

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 12.4.1 | Expiry Interval decremented in forwarded PUBLISH | ✓ | |
| 12.4.2 | Message expired before delivery → dropped | ✓ | |
| 12.4.3 | Offline queue: expired messages removed before delivery on reconnect | ✓ | |

### 12.5 User Properties

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 12.5.1 | User Properties forwarded | ✓ | |
| 12.5.2 | Multiple User Properties all forwarded | ✓ | |

---

## 13. Error Handling & Protocol Conformance (`error_handling_protocol_conformance.py`)

### 13.1 Malformed Packet Handling

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 13.1.1 | Truncated packet → DISCONNECT 0x81 | ✓ | |
| 13.1.2 | Invalid remaining length encoding → DISCONNECT 0x81 | ✓ | |
| 13.1.3 | Reserved bits in Fixed Header → DISCONNECT 0x81 | ✓ | |
| 13.1.4 | Invalid UTF-8 in topic → DISCONNECT 0x81 | ✓ | |
| 13.1.5 | Unknown property ID → DISCONNECT 0x81 | ✓ | |
| 13.1.6 | Duplicate non-repeatable property → DISCONNECT 0x82 | ✓ | |

### 13.2 Protocol Errors

| ID | Description | Status | Notes |
|----|-------------|--------|-------|
| 13.2.1 | Packet type not in Connected state → DISCONNECT 0x82 | ✓ | |
| 13.2.2 | SUBSCRIBE with empty filter list → DISCONNECT 0x82 | ✓ | |
| 13.2.3 | PUBLISH QoS 3 (invalid) → connection closed | ✓ | |
| 13.2.4 | PUBREL for unknown Packet ID → reason code 0x92 | ✓ | |

### 13.3 Reason Code Coverage

| ID | Reason Code | Status | Notes |
|----|-------------|--------|-------|
| 13.3.1 | 0x00 — Success in all ACK types | ✓ | |
| 13.3.2 | 0x10 — No matching subscribers (PUBACK) | ✓ | |
| 13.3.3 | 0x80 — Unspecified error | ⚠ | Test sends the same truncated CONNECT packet used in 13.1.1 (which correctly produces 0x81 — Malformed Packet). Expecting 0x80 (Unspecified error) for this scenario is **not spec-conformant**: MQTT 5.0 §4.13 requires the most specific reason code; 0x81 is the correct code for a malformed packet. The test thereby permits the broker to return 0x80 instead of 0x81, masking a protocol violation. A proper 0x80 test would need a scenario where the spec genuinely permits 0x80 (e.g. an internal broker error). |
| 13.3.4 | 0x81 — Malformed Packet | ✓ | |
| 13.3.5 | 0x82 — Protocol Error | ✓ | |
| 13.3.6 | 0x83 — Implementation Specific Error | ⚠ | Test subscribes to a topic, then publishes QoS1 to that same topic, and expects PUBACK reason 0x83. A normal authorized publish to a subscribed topic must return 0x00 (Success). The spec defines 0x83 as a reason a broker may use when it chooses not to process a PUBLISH for implementation-specific reasons — but there is no legitimate reason for the broker to reject a normal authorized QoS1 publish with 0x83. This test validates undefined/non-standard behavior and would pass incorrectly if the broker returned 0x00 (the correct response). |
| 13.3.7 | 0x86 — Bad User Name or Password | ✓ | |
| 13.3.8 | 0x87 — Not Authorized | ✓ | |
| 13.3.9 | 0x8B — Server shutting down | ✓ | Stops broker, waits for DISCONNECT 0x8B |
| 13.3.10 | 0x8D — Keep Alive timeout | ✓ | Connect keepalive=1, waits for DISCONNECT 0x8D |
| 13.3.11 | 0x8E — Session taken over | ✓ | |
| 13.3.12 | 0x91 — Packet Identifier in use | ✓ | |
| 13.3.13 | 0x92 — Packet Identifier not found | ✓ | |
| 13.3.14 | 0x93 — Receive Maximum exceeded | ✓ | |
| 13.3.15 | 0x95 — Packet too large | ✓ | |
| 13.3.16 | 0x97 — Quota exceeded | ✓ | |
| 13.3.17 | 0x9E — Shared Subscriptions not supported | ✓ | |

---

## Summary

| Section | Total | ✓ | ⚠ | ✗ |
|---------|-------|---|---|---|
| 0 (Prerequisites) | 26 | 26 | 0 | 0 |
| 1 (Connection) | 37 | 35 | 2 | 0 |
| 2 (Publish/Subscribe) | 27 | 27 | 0 | 0 |
| 3 (Topic Matching) | 18 | 18 | 0 | 0 |
| 4 (Retained Messages) | 12 | 11 | 1 | 0 |
| 5 (Will Messages) | 12 | 12 | 0 | 0 |
| 6 (Session Persistence) | 12 | 12 | 0 | 0 |
| 7 (Subscription Options) | 9 | 9 | 0 | 0 |
| 8 (Topic Alias) | 7 | 7 | 0 | 0 |
| 9 (Flow Control) | 6 | 6 | 0 | 0 |
| 10 (Authentication) | 9 | 9 | 0 | 0 |
| 11 (Authorization) | 6 | 6 | 0 | 0 |
| 12 (Message Properties) | 11 | 11 | 0 | 0 |
| 13 (Error Handling) | 21 | 18 | 3 | 0 |
| **Total** | **213** | **207** | **6** | **0** |

### Issues requiring attention

1. **1.2.4** — `ReceiveMaximum` throttling not verified. The test confirms delivery with `ReceiveMaximum=1` but does not test that the broker withholds a second PUBLISH while the first is unacknowledged. The actual back-pressure enforcement is covered by 9.1.1 but that tests the outbound direction (broker → client), not the client-advertised inflight limit.

2. **1.8.4** — Abrupt-close detection conflated with keep-alive timeout. The will message is only observed after the keep-alive period elapses, so the test does not cleanly isolate TCP RST detection from keep-alive expiry.

3. **4.1.3** — Default retained delivery RETAIN flag behavior not verified. Using `retainAsPublished=True` to observe RETAIN=1 tests 4.4.1 behavior; the spec default (RETAIN cleared on forwarding) for a retained delivery is not independently asserted.

4. **13.3.3** — Wrong reason code expected for malformed packet. A truncated CONNECT produces 0x81 (Malformed Packet). Accepting 0x80 (Unspecified error) for this case allows the broker to return a less-specific code than the spec requires.

5. **13.3.6** — No valid scenario for 0x83. A normal authorized QoS1 publish to a subscribed topic must return 0x00. Expecting 0x83 here either tests a non-existent broker behavior or masks a broker that incorrectly rejects authorized publishes.
