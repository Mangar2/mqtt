# MQTT 5.0 Client – Implementation Plan

Step-by-step roadmap for building an MQTT 5.0 client library that is compatible with the existing broker,
followed by a test client built on top of that library.
Each step is independently implementable and produces a well-defined result.

---

## Phase 1 – Primitives and Shared Data Types

### Step 1 – MQTT Primitive Type Helpers

Implement helpers that convert between the basic on-wire byte formats defined by the MQTT 5.0 specification
and native language types: variable-length integer, two-byte integer, four-byte integer, UTF-8 string,
UTF-8 string pair, and binary data.

**Result:** Every other component in the library has a single, tested utility layer for reading and writing
raw MQTT wire types. No component needs to handle byte arithmetic itself.

**Implementation status (2026-04-27): Completed via existing broker codec (no extra facade layer required)**

Existing reusable primitives:
- `src/data_model/types/variable_byte_integer.h`: `mqtt::VariableByteInteger`
- `src/data_model/types/integers.h`: `mqtt::TwoByteInteger`, `mqtt::FourByteInteger`
- `src/data_model/types/utf8_string.h`: `mqtt::Utf8String`, `mqtt::Utf8StringPair`
- `src/data_model/types/binary_data.h`: `mqtt::BinaryData`

Existing reusable primitive wire helpers (already in server code):
- `src/codec/primitive/primitive_codec.h`
	- Encode: `encode_byte`, `encode_variable_byte_integer`, `encode_two_byte_integer`, `encode_four_byte_integer`, `encode_utf8_string`, `encode_utf8_string_pair`, `encode_binary_data`
	- Decode: `decode_byte`, `decode_variable_byte_integer`, `decode_two_byte_integer`, `decode_four_byte_integer`, `decode_utf8_string`, `decode_utf8_string_pair`, `decode_binary_data`

Verification:
- Existing primitive unit tests remain valid in `src/codec/primitive/test/primitive_codec_test.cpp`.
- No additional wrapper-specific tests are required because client and server both use the same existing primitive helpers directly.

---

### Step 2 – Reason Code Registry

Define all 39 MQTT 5.0 reason codes as named constants together with a human-readable description and a
classification of success or error.

**Result:** Any component can refer to reason codes by name. Callers can check whether an outcome is a
success or an error without interpreting raw numeric values.

**Implementation status (2026-04-27): Completed via existing broker data model (no extra client-specific layer required)**

Existing reusable reason-code types and helpers:
- `src/data_model/reason_code/reason_code.h`
	- `mqtt::ReasonCode` (all 39 distinct MQTT 5.0 reason-code wire values)
	- `mqtt::k_normal_disconnection`, `mqtt::k_granted_qos0` (spec aliases)
	- `mqtt::is_success(ReasonCode)`, `mqtt::is_error(ReasonCode)` (success/error classification)
	- `mqtt::qos_to_granted_reason(QoS)` (SUBACK granted-QoS mapping helper)

Existing verification:
- `src/data_model/reason_code/test/reason_code_test.cpp`
	- verifies representative wire values, alias mapping, success/error boundary behavior, and QoS-to-reason mapping.

Notes:
- Human-readable naming is already provided by the `ReasonCode` enumerator names and their API documentation in `reason_code.h`.
- No additional wrapper or duplicate registry is required for client usage; client and server can reuse the same reason-code model directly.

---

### Step 3 – Property Definitions

Define all 27 MQTT 5.0 property identifiers with their associated data types and the list of packet types
where each property is permitted.

**Result:** Properties can be referenced by name throughout the library. Incorrect use of a property
in a packet type where it is not allowed can be detected early, before bytes are sent on the wire.

**Implementation status (2026-04-27): Completed via existing broker data model (no extra client-specific layer required)**

Existing reusable property definitions:
- `src/data_model/property/property_id.h`
	- `mqtt::PropertyId` with all 27 MQTT 5.0 property identifiers.
- `src/data_model/property/property.h`
	- `mqtt::PropertyValue` (`std::variant` of all MQTT property wire data types)
	- `mqtt::Property` (ID + typed value)

Existing reusable property maps and validation primitives:
- `src/data_model/property/property_maps.h`
	- `mqtt::PropertyDataType`
	- `mqtt::property_data_type(PropertyId)` for ID-to-data-type mapping
	- `mqtt::is_property_allowed(PropertyId, PacketType)` for allowed packet-context mapping (including `PacketType::Will`)

Existing verification:
- `src/data_model/property/test/property_test.cpp`
	- verifies representative `PropertyId` wire values,
	- verifies property-to-data-type mapping,
	- verifies property-to-packet-type allow/deny mapping paths.

Notes:
- Step 3 data definitions and allow-lists are already present and directly reusable for both client and server.
- No additional wrapper or duplicate property registry is required for client usage.

---

### Step 4 – Packet Data Structures

Define plain data structures for all 15 MQTT packet types: CONNECT, CONNACK, PUBLISH, PUBACK, PUBREC,
PUBREL, PUBCOMP, SUBSCRIBE, SUBACK, UNSUBSCRIBE, UNSUBACK, PINGREQ, PINGRESP, DISCONNECT, and AUTH.
Each structure holds exactly the fields the specification defines for that packet.

**Result:** Every component shares the same in-memory representation of MQTT packets. There is no
duplication of field definitions anywhere in the codebase.

**Implementation status (2026-04-27): Completed via existing broker data model (no extra client-specific layer required)**

Existing reusable packet type and packet structures:
- `src/data_model/packet/packet_type.h`
	- `mqtt::PacketType` for all MQTT control packet types (`Connect`..`Auth`) plus internal `Will` context marker.
- `src/data_model/packet/connect_packet.h`
	- `mqtt::WillData`
	- `mqtt::ConnectPacket` (CONNECT)
	- `mqtt::ConnackPacket` (CONNACK)
- `src/data_model/packet/publish_packets.h`
	- `mqtt::PublishPacket` (PUBLISH)
	- `mqtt::PubackPacket` (PUBACK)
	- `mqtt::PubrecPacket` (PUBREC)
	- `mqtt::PubrelPacket` (PUBREL)
	- `mqtt::PubcompPacket` (PUBCOMP)
- `src/data_model/packet/subscribe_packets.h`
	- `mqtt::SubscribeOptions`, `mqtt::SubscribeFilter`
	- `mqtt::SubscribePacket` (SUBSCRIBE)
	- `mqtt::SubackPacket` (SUBACK)
	- `mqtt::UnsubscribePacket` (UNSUBSCRIBE)
	- `mqtt::UnsubackPacket` (UNSUBACK)
	- helper: `subscription_identifier_from(const SubscribePacket&)`
- `src/data_model/packet/control_packets.h`
	- `mqtt::PingreqPacket` (PINGREQ)
	- `mqtt::PingrespPacket` (PINGRESP)
	- `mqtt::DisconnectPacket` (DISCONNECT)
	- `mqtt::AuthPacket` (AUTH)

Existing verification:
- `src/data_model/packet/test/packet_test.cpp`
	- verifies default state and equality behavior for the packet structs,
	- verifies representative field behavior (e.g. optional IDs / helper extraction).

Notes:
- Step 4 packet data structures are already present and directly reusable for both client and server.
- No additional wrapper or duplicate packet model layer is required for client usage.

---

## Phase 2 – Protocol Codec

### Step 5 – Primitive Encoders and Decoders

Implement encode and decode functions for every basic MQTT wire type defined in Step 1: write a value
to a byte buffer and read a value from a byte buffer at a given position.

**Result:** The raw byte-level translation between native types and MQTT wire format is fully covered.
All higher-level codec components build on these functions.

**Implementation status (2026-04-27): Completed via existing broker codec module (no extra client-specific layer required)**

Existing reusable primitive codec API:
- `src/codec/primitive/primitive_codec.h`
	- Encoding: `encode_byte`, `encode_variable_byte_integer`, `encode_two_byte_integer`, `encode_four_byte_integer`, `encode_utf8_string`, `encode_utf8_string_pair`, `encode_binary_data`
	- Decoding: `decode_byte`, `decode_variable_byte_integer`, `decode_two_byte_integer`, `decode_four_byte_integer`, `decode_utf8_string`, `decode_utf8_string_pair`, `decode_binary_data`

Data model types used directly by the primitive codec:
- `src/data_model/types/variable_byte_integer.h`
- `src/data_model/types/integers.h`
- `src/data_model/types/utf8_string.h`
- `src/data_model/types/binary_data.h`

Existing verification:
- `src/codec/primitive/test/TEST_SPEC.md`
- `src/codec/primitive/test/primitive_codec_test.cpp`
	- covers encode/decode behavior and roundtrips for byte, VBI, two-byte integer, four-byte integer, UTF-8 string, UTF-8 string pair, and binary data,
	- covers error paths (buffer too short, malformed UTF-8, VBI overflow, string length limits).

Notes:
- Step 5 functionality is already implemented and reused by broker packet/property codecs.
- No additional wrapper or duplicate primitive codec layer is required for client usage.

---

### Step 6 – Properties Codec

Implement encoding of a property list to bytes and decoding of a byte sequence back to a property list.
Include validation that each property has the correct data type and appears only in packet types
where it is allowed, and that no property is present more than once unless the specification explicitly permits it.

**Result:** The properties section of any MQTT packet can be serialized and deserialized correctly.
Invalid property usage is rejected before bytes leave the application.

**Implementation status (2026-04-27): Completed via existing broker codec module (no extra client-specific layer required)**

Existing reusable properties codec API:
- `src/codec/properties/properties_codec.h`
	- `encode_properties(WriteBuffer&, const std::vector<Property>&, PacketType)`
	- `decode_properties(ReadBuffer&, PacketType)`

Existing validation behavior in implementation:
- `src/codec/properties/properties_codec.cpp`
	- property value type validation (`PropertyTypeMismatch`)
	- property allowed-in-packet validation (`PropertyNotAllowed`)
	- duplicate detection for non-repeatable properties (`DuplicateProperty`)
	- unknown property-ID rejection during decode (`InvalidPropertyId`)

Dependencies reused directly:
- `src/data_model/property/property.h`
- `src/data_model/property/property_id.h`
- `src/data_model/property/property_maps.h`
- `src/codec/primitive/primitive_codec.h`

Existing verification:
- `src/codec/properties/test/TEST_SPEC.md`
- `src/codec/properties/test/properties_codec_test.cpp`
	- covers encode/decode for all supported property wire types,
	- covers packet-context allow/deny behavior,
	- covers duplicate/non-duplicate rules (including repeatable `UserProperty`),
	- covers decode invalid/truncated input and roundtrip behavior.

Notes:
- Step 6 functionality is already implemented and used by packet codecs.
- No additional wrapper or duplicate properties codec layer is required for client usage.

---

### Step 7 – Fixed Header and Remaining Length Codec

Implement encoding and decoding of the MQTT fixed header: the packet type byte with its flags and the
variable-length remaining-length field that follows it.

**Result:** Packet framing is available for all packet types. Every encoder and decoder can prepend or
parse a correct fixed header.

**Implementation status (2026-04-27): Completed via existing broker codec module (no extra client-specific layer required)**

Existing reusable fixed-header data model and codec API:
- `src/codec/fixed_header/fixed_header.h`
	- `mqtt::FixedHeader` (`PacketType`, flags, remaining_length)
- `src/codec/fixed_header/fixed_header_codec.h`
	- `encode_fixed_header(WriteBuffer&, const FixedHeader&)`
	- `decode_fixed_header(ReadBuffer&)`

Existing behavior and validation in implementation:
- `src/codec/fixed_header/fixed_header_codec.cpp`
	- encode/decode of first fixed-header byte (`type << 4 | flags`)
	- encode/decode of Remaining Length via MQTT VBI
	- packet-type validation (`InvalidPacketType` for reserved type nibble 0)
	- per-packet fixed-flag validation (`InvalidFlags` for invalid reserved flags)

Dependencies reused directly:
- `src/data_model/packet/packet_type.h`
- `src/codec/primitive/primitive_codec.h`

Existing verification:
- `src/codec/fixed_header/test/TEST_SPEC.md`
- `src/codec/fixed_header/test/fixed_header_codec_test.cpp`
	- covers single- and multi-byte Remaining Length paths,
	- covers required flag combinations for packet types,
	- covers malformed input/error behavior and roundtrip behavior.

Notes:
- Step 7 framing functionality is already implemented and used by packet codecs and stream framing logic.
- No additional wrapper or duplicate fixed-header codec layer is required for client usage.

---

### Step 8 – Outbound Packet Encoders

Implement one encoder per packet type that the client sends: CONNECT, PUBLISH, PUBACK, PUBREC, PUBREL,
PUBCOMP, SUBSCRIBE, UNSUBSCRIBE, PINGREQ, DISCONNECT, and AUTH. Each encoder takes the corresponding
data structure from Step 4 and produces a complete byte sequence ready to be sent over the network.

**Result:** The client can produce valid wire-format bytes for every outbound packet type the
MQTT 5.0 specification defines for clients.

**Implementation status (2026-04-27): Completed via existing broker codec module (no extra client-specific layer required)**

Existing reusable outbound packet encoders:
- `src/codec/packet/connect_codec.h`
	- `encode_connect` (CONNECT)
- `src/codec/packet/publish_codec.h`
	- `encode_publish` (PUBLISH)
	- `encode_puback`, `encode_pubrec`, `encode_pubrel`, `encode_pubcomp`
- `src/codec/packet/subscribe_codec.h`
	- `encode_subscribe` (SUBSCRIBE)
	- `encode_unsubscribe` (UNSUBSCRIBE)
- `src/codec/packet/control_codec.h`
	- `encode_pingreq` (PINGREQ)
	- `encode_disconnect` (DISCONNECT)
	- `encode_auth` (AUTH)

Existing verification:
- `src/codec/packet/test/TEST_SPEC.md`
- `src/codec/packet/test/connect_codec_test.cpp`
- `src/codec/packet/test/publish_codec_test.cpp`
- `src/codec/packet/test/subscribe_codec_test.cpp`
- `src/codec/packet/test/control_codec_test.cpp`
	- includes roundtrip and error-path coverage for outbound encoder behavior.

Notes:
- Step 8 outbound packet encoders are already implemented and used by broker-side flows.
- No additional wrapper or duplicate outbound codec layer is required for client usage.

---

### Step 9 – Inbound Packet Decoders

Implement one decoder per packet type that the broker sends to a client: CONNACK, PUBLISH, PUBACK,
PUBREC, PUBREL, PUBCOMP, SUBACK, UNSUBACK, PINGRESP, DISCONNECT, and AUTH. Each decoder takes a
byte buffer and produces the corresponding data structure from Step 4.

**Result:** Every packet type the broker can send is converted to a typed in-memory structure.
The rest of the library never has to parse raw bytes.

**Implementation status (2026-04-27): Completed via existing broker codec module (no extra client-specific layer required)**

Existing reusable inbound packet decoders (broker -> client subset):
- `src/codec/packet/connect_codec.h`
	- `decode_connack` (CONNACK)
- `src/codec/packet/publish_codec.h`
	- `decode_publish` (PUBLISH)
	- `decode_puback`, `decode_pubrec`, `decode_pubrel`, `decode_pubcomp`
- `src/codec/packet/subscribe_codec.h`
	- `decode_suback` (SUBACK)
	- `decode_unsuback` (UNSUBACK)
- `src/codec/packet/control_codec.h`
	- `decode_pingresp` (PINGRESP)
	- `decode_disconnect` (DISCONNECT)
	- `decode_auth` (AUTH)

Existing verification:
- `src/codec/packet/test/TEST_SPEC.md`
- `src/codec/packet/test/connect_codec_test.cpp`
- `src/codec/packet/test/publish_codec_test.cpp`
- `src/codec/packet/test/subscribe_codec_test.cpp`
- `src/codec/packet/test/control_codec_test.cpp`
	- includes decode success/error coverage for all broker-to-client packet types listed in Step 9.

Notes:
- Step 9 inbound packet decoders are already implemented and used in packet-reading/dispatch paths.
- No additional wrapper or duplicate inbound codec layer is required for client usage.

---

### Step 10 – Stream Framer

Implement a component that accepts an incoming byte stream and detects where each complete MQTT packet
starts and ends, using the fixed header and remaining length field. Incomplete packets are buffered
until the missing bytes arrive; complete packets are handed off immediately.

**Result:** The network layer can deliver partial reads to this component without any packet being
lost or incorrectly split. The decoder layer always receives exactly one complete packet at a time.

**Implementation status (2026-04-27): Completed via existing network layer stream framer (no extra client-specific layer required)**

Existing reusable stream-framing component:
- `src/network/stream_buffer.h`
	- `mqtt::StreamBuffer` with byte-stream accumulation and complete-packet extraction
	- `append(std::span<const uint8_t>)`
	- `has_complete_packet()`
	- `consume_packet()`

Existing framing behavior in implementation:
- `src/network/stream_buffer.cpp`
	- parses MQTT Remaining Length (VBI) from the fixed header
	- computes total packet size from fixed-header bytes + remaining length
	- buffers incomplete packets until enough bytes arrive
	- returns exactly one complete packet per `consume_packet()` call
	- supports fragmented delivery and chunk-boundary splits

Existing verification:
- `src/network/test/network_test.cpp`
	- covers single/multi-byte Remaining Length parsing,
	- covers fragmented delivery and multi-packet coalescing,
	- covers zero-length payload packets and malformed/incomplete scenarios,
	- covers bounded-buffer behavior and repeated consume cycles.

Notes:
- Step 10 stream framing functionality is already implemented in the network module and used by connection processing paths.
- No additional wrapper or duplicate stream framer layer is required for client usage.

---

## Phase 3 – Network Transport

### Step 11 – Plain TCP Connection

Implement the ability to open a TCP connection to a given host and port, send a byte buffer, receive
bytes into a buffer, and close the connection cleanly. No MQTT logic lives here.

**Result:** Raw bytes can be exchanged with the broker over a plain TCP socket. The transport layer
is a self-contained unit that higher layers depend on but do not need to understand.

**Implementation status (2026-04-27): Partially completed via existing network layer primitives**

Existing reusable TCP transport primitives:
- `src/network/tcp_connection.h`
	- `mqtt::TcpConnection` for connected-socket I/O (`read`, `write`, `close`, timeout support)
- `src/network/network_error.h`
	- network error taxonomy for transport failures

Existing verification:
- `src/network/test/network_test.cpp`
	- covers connection read/write/close behavior and EOF/timeout related paths.

Gap for full client Step 11 completion:
- There is no dedicated client-side connector abstraction yet that takes `host + port` and establishes outbound TCP directly as a reusable API (current code mainly wraps already-connected sockets and server-side accept/listen flow).

Notes:
- Core I/O behavior for a plain TCP transport is present and reusable.
- A thin client dial/connect component can be added later to complete Step 11 as a client-facing API.

---

### Step 12 – WebSocket Transport

Extend the transport layer to support WebSocket connections. This includes performing the HTTP/1.1
upgrade handshake and wrapping each outbound MQTT byte sequence in a WebSocket frame. Inbound frames
are stripped of their WebSocket framing before the bytes are passed up to the stream framer.
The interface exposed to the rest of the library is identical to the plain TCP connection.

**Result:** The client can connect to the broker's WebSocket endpoint. All higher-level components
work without modification regardless of whether the underlying transport is plain TCP or WebSocket.

**Implementation status (2026-04-27): Partially completed via existing WebSocket transport components**

Existing reusable WebSocket transport components:
- `src/transport/websocket_handshake.h/.cpp`
	- server-side HTTP upgrade request parsing and 101 response generation
- `src/transport/websocket_frame_codec.h/.cpp`
	- WebSocket frame encode/decode for MQTT binary payload transport
- `src/transport/websocket_transport.h/.cpp`
	- composed transport over `TcpConnection` with WS framing and control-frame handling

Existing verification:
- `src/transport/test/TEST_SPEC.md`
- `src/transport/test/websocket_test.cpp`
	- covers handshake, frame codec behavior, and composed transport runtime behavior.

Gap for full client Step 12 completion:
- Current handshake implementation is server-side (expects incoming upgrade request, returns 101 response).
- A dedicated client-side WebSocket initiator (send upgrade request, validate 101 response, then run MQTT-over-WS stream) is still required for a full outbound client transport.

Notes:
- Framing and runtime WS transport logic are already present and can be reused heavily.
- A client-specific handshake initiator layer is the main remaining piece for Step 12 completion.

---

## Phase 4 – MQTT Protocol Engine

### Step 13 – Packet Identifier Manager

Implement a component that allocates and releases 16-bit packet identifiers for outbound flows
and tracks which identifiers are currently in use for inbound flows. The inbound and outbound
identifier spaces are kept separate.

**Result:** QoS 1 and QoS 2 messages can each be assigned a unique identifier without collisions.
The manager ensures that identifiers are never reused while a previous operation with the same
identifier is still pending.

**Implementation status (2026-04-27): Completed via existing QoS module (no extra client-specific layer required)**

Existing reusable packet-identifier manager:
- `src/qos/packet_id_manager.h/.cpp`
	- `PacketIdManager::allocate()` for outbound ID allocation in range 1..65535 with wraparound scan
	- `PacketIdManager::try_register_inbound(id)` for inbound duplicate detection tracking
	- `PacketIdManager::release(id, dir)` and `is_in_use(id, dir)`
	- separate inbound/outbound ID spaces via `InflightDirection`
	- `register_existing(id, dir)` for session-resume/persisted inflight restoration paths

Dependencies reused directly:
- `src/data_model/session/inflight_direction.h`
- `src/qos/qos_error.h`

Existing verification:
- `src/qos/test/TEST_SPEC.md`
- `src/qos/test/qos_test.cpp`
	- covers allocation range, sequential allocation, wraparound behavior, exhaustion error,
	- covers release/reuse and in-use queries,
	- covers inbound duplicate registration and separation of inbound/outbound spaces.

Notes:
- Step 13 functionality is already implemented and used by QoS 1 and QoS 2 state machines.
- No additional wrapper or duplicate packet-id manager layer is required for client usage.

---

### Step 14 – QoS 1 Engine

Implement the QoS 1 state machine for both directions. For outbound messages: send PUBLISH, wait
for PUBACK, and mark the message as complete. For inbound messages: receive PUBLISH and send PUBACK.
Pending outbound messages survive a connection drop and are retransmitted on reconnect with the
DUP flag set.

**Result:** Messages with QoS 1 are delivered at-least-once in both directions. The caller submits
a message and is notified when the broker has acknowledged it; retransmission happens automatically.

**Implementation status (2026-04-27): Completed via existing QoS module (no extra client-specific layer required)**

Existing reusable QoS 1 state machine:
- `src/qos/qos1_state_machine.h/.cpp`
	- inbound path: `on_publish_received(pkt)` validates QoS1 PUBLISH and returns PUBACK
	- outbound path: `initiate_publish(msg)` creates outbound inflight entry and returns initial PUBLISH
	- acknowledgement path: `on_puback_received(pkt)` completes exchange and releases Packet ID
	- reconnect/retry path: `retransmit(packet_id)` rebuilds PUBLISH with `dup=true` and refreshes timestamp

Dependencies reused directly:
- `src/qos/packet_id_manager.h/.cpp`
- `src/store/inflight_store.h`
- `src/data_model/session/inflight_state.h`
- `src/data_model/session/inflight_direction.h`

Existing verification:
- `src/qos/test/TEST_SPEC.md`
- `src/qos/test/qos_test.cpp`
	- covers inbound PUBACK generation and invalid packet rejection,
	- covers outbound initiation and inflight entry creation,
	- covers PUBACK completion/release behavior,
	- covers retransmit with DUP flag and timestamp refresh.

Notes:
- Step 14 behavior is already implemented for both directions.
- Pending outbound QoS 1 operations are represented in `InflightStore` and can be retransmitted via `retransmit(...)` after reconnect orchestration.

---

### Step 15 – QoS 2 Engine

Implement the QoS 2 state machine for both directions. The four-step handshake
(PUBLISH → PUBREC → PUBREL → PUBCOMP) is tracked for every in-flight message. Duplicate inbound
PUBLISH packets are detected and suppressed. Pending operations survive a connection drop and
resume from their last confirmed phase on reconnect.

**Result:** Messages with QoS 2 are delivered exactly-once in both directions. The caller submits
a message and is notified only after the complete four-step cycle has finished.

**Implementation status (2026-04-27): Completed via existing QoS module (no extra client-specific layer required)**

Existing reusable QoS 2 state machine:
- `src/qos/qos2_state_machine.h/.cpp`
	- inbound handshake: `on_publish_received(pkt)` -> PUBREC with duplicate detection
	- inbound completion: `on_pubrel_received(pkt)` -> PUBCOMP and inbound state cleanup
	- outbound handshake start: `initiate_publish(msg)` -> PUBLISH with new Packet ID
	- outbound phase advance: `on_pubrec_received(pkt)` -> PUBREL
	- outbound completion: `on_pubcomp_received(pkt)` -> inflight removal + Packet ID release
	- reconnect/retry path: `retransmit(packet_id)` returns DUP PUBLISH or PUBREL by phase

Duplicate suppression behavior:
- inbound duplicate PUBLISH detection uses `PacketIdManager::try_register_inbound(...)`
- duplicate PUBREL after completion is handled idempotently (PUBCOMP resent)

Dependencies reused directly:
- `src/qos/packet_id_manager.h/.cpp`
- `src/store/inflight_store.h`
- `src/data_model/session/inflight_state.h`
- `src/data_model/session/inflight_direction.h`

Existing verification:
- `src/qos/test/TEST_SPEC.md`
- `src/qos/test/qos_test.cpp`
	- covers full inbound and outbound QoS 2 transitions,
	- covers duplicate detection and idempotent duplicate-phase handling,
	- covers retransmission behavior for both handshake phases,
	- covers completion and unknown-packet-id error paths.

Notes:
- Step 15 behavior is already implemented for both directions including duplicate handling.
- Pending QoS 2 operations are represented in `InflightStore` and can resume from stored phase via `retransmit(...)` once reconnect orchestration is active.

---

### Step 16 – Keep-Alive Manager

Implement a timer-based component that sends a PINGREQ whenever the connection has been idle for
the negotiated keep-alive interval. If no PINGRESP is received within a configurable deadline,
the connection is considered lost and a failure is signalled to the session layer.

**Result:** Idle connections are kept alive according to the negotiated interval. Silent TCP failures
are detected promptly without relying on the operating system's TCP timeout, which can take minutes.

**Implementation status (2026-04-27): Completed (client-side module implemented)**

Implemented client-side keep-alive manager:
- `src/client/keep_alive_manager.h/.cpp`
	- active idle polling that emits `SendPingreq` after keep-alive interval
	- explicit wait state for `PINGRESP` with timeout transition (`Timeout`)
	- traffic hooks (`note_activity`, `on_pingresp`) to refresh/clear state
	- helper to generate wire-ready MQTT `PINGREQ` frame

Existing related reusable components:
- `src/codec/packet/control_codec.h/.cpp` (PINGREQ/PINGRESP encode/decode)
- `src/connection/keep_alive_timer.h/.cpp` (broker-side timeout enforcement)

Verification:
- `src/client/test/TEST_SPEC.md`
- `src/client/test/client_test.cpp`
	- disabled-mode behavior,
	- ping emission after idle,
	- timeout when `PINGRESP` is missing,
	- `PINGRESP` state-clear behavior.

---

### Step 17 – Topic Alias Manager (outbound)

Implement tracking of the outbound topic-alias-to-topic mapping. On the first PUBLISH to a given
topic the alias is registered; subsequent publishes to the same topic use the numeric alias instead
of the full topic string. The number of active aliases never exceeds the broker's Topic Alias Maximum
reported in CONNACK.

**Result:** High-frequency publishes to the same topic are sent with a compact numeric alias.
Wire overhead is reduced automatically for repeated topics without any change in the caller's API.

**Implementation status (2026-04-27): Completed (client-side module implemented)**

Implemented outbound alias manager:
- `src/client/outbound_topic_alias_manager.h/.cpp`
	- first publish assigns alias and keeps topic
	- repeated publish reuses alias and compacts packet by clearing topic string
	- enforces configured alias maximum and bounded active mapping set
	- supports mapping reset on connection lifecycle reset

Existing related reusable components:
- `src/data_model/property/property_id.h` (`TopicAlias` / `TopicAliasMaximum`)
- `src/connection/topic_alias_table.h/.cpp` (existing alias table primitives)

Verification:
- `src/client/test/TEST_SPEC.md`
- `src/client/test/client_test.cpp`
	- disabled mode,
	- first publish assignment,
	- repeated publish compaction,
	- invalid empty-topic rejection path.

---

## Phase 5 – Client Session Logic

### Step 18 – Connection Negotiator

Implement the component that builds a CONNECT packet from the caller's parameters, sends it to
the broker, reads the CONNACK response, and returns the negotiated session state to the caller.
Failure reason codes are translated into meaningful error values.

**Result:** A complete MQTT handshake can be performed with one call. The caller learns whether
the session was resumed from a previous connection or created fresh, and receives the broker's
negotiated limits (receive maximum, topic alias maximum, assigned client ID).

**Implementation status (2026-04-27): Completed (client-side module implemented)**

Implemented client-side connection negotiator:
- `src/client/connection_negotiator.h/.cpp`
	- `dial_tcp(host, port)` for outbound TCP dial
	- `negotiate(connection, connect_packet, timeout)` to send CONNECT and wait for first response
	- validates first response is CONNACK
	- maps error CONNACK reason codes to typed client exception (`NegotiationRejected`)
	- extracts negotiated values (`session_present`, `ReceiveMaximum`, `TopicAliasMaximum`, optional `ServerKeepAlive`, optional assigned client ID)

Existing related reusable components:
- `src/codec/packet/connect_codec.h/.cpp`
- `src/codec/packet_reader/packet_reader.h/.cpp`
- `src/network/stream_buffer.h/.cpp`

Verification:
- `src/client/test/TEST_SPEC.md`
- `src/client/test/client_test.cpp`
	- successful CONNACK negotiation path,
	- rejected CONNACK mapping,
	- non-CONNACK protocol error handling,
	- invalid-host dial failure path.

---

### Step 19 – Session State Keeper

Implement storage for the client-side session state: the set of active subscriptions with their
QoS levels, the list of currently inflight messages, and the session expiry interval. When the
client reconnects with clean-start set to false, the persisted state is used to restore subscriptions
and replay inflight messages.

**Result:** A persistent session survives connection drops without the caller having to re-subscribe
or re-publish anything. QoS 1 and QoS 2 messages that were in flight at the time of the disconnect
are automatically retransmitted after reconnect.

**Implementation status (2026-04-27): Completed (client-side module implemented)**

Implemented client-side session-state keeper:
- `src/client/session_state_keeper.h/.cpp`
	- stores active subscriptions (upsert/remove/clear)
	- stores session expiry interval
	- stores/captures outbound inflight entries for replay
	- builds reconnect restore plan depending on `clean_start`
	- supports full snapshot export/import with client-id consistency guard

Existing related reusable components:
- `src/data_model/session/session_state.h`
- `src/data_model/session/inflight_entry.h`
- `src/store/inflight_store.h`

Verification:
- `src/client/test/TEST_SPEC.md`
- `src/client/test/client_test.cpp`
	- subscription state upsert/remove/clear behavior,
	- restore-plan behavior for `clean_start=true/false`,
	- outbound inflight filtering and deterministic ordering,
	- capture from shared inflight store,
	- snapshot roundtrip and mismatch-guard behavior.

---

### Step 20 – Subscription Manager (client-side)

Implement the component that sends SUBSCRIBE and UNSUBSCRIBE packets, matches the corresponding
SUBACK and UNSUBACK responses, and maintains a local table of active topic filters paired with
their message-delivery callbacks. Inbound PUBLISH packets are matched against this table and the
correct callback is invoked.

**Result:** Callers can subscribe to topics with a single call and receive a confirmation with
the granted QoS level. Incoming messages are dispatched to the registered handler automatically.
Subscribing and unsubscribing are managed independently for each topic filter.

**Implementation status (2026-04-27): Completed (client-side module implemented)**

Implemented client-side subscription manager:
- `src/client/subscription_manager.h/.cpp`
	- builds outbound `SUBSCRIBE` and `UNSUBSCRIBE` packets with packet-id allocation,
	- tracks pending operations and correlates `SUBACK` / `UNSUBACK`,
	- maintains active topic-filter callback table,
	- activates accepted subscriptions from `SUBACK` reason codes,
	- removes successful unsubscriptions from `UNSUBACK` reason codes,
	- dispatches inbound `PUBLISH` packets to matching callbacks via topic matcher.

Existing related reusable components:
- `src/data_model/packet/subscribe_packets.h`
- `src/topic/subscription_trie.h`
- `src/topic/topic_matcher.h`
- `src/topic/topic_validator.h`

Verification:
- `src/client/test/TEST_SPEC.md`
- `src/client/test/client_test.cpp`
	- subscribe/unsubscribe packet build and ack correlation,
	- ack mismatch/unknown-packet-id error paths,
	- inbound publish callback dispatch with wildcard filter matching,
	- invalid filter/topic validation handling.

---

### Step 21 – Publish Pipeline

Implement the outbound message path: accept a message from the caller, assign a packet identifier
for QoS 1 and QoS 2, build the PUBLISH packet including any properties, send it over the transport,
and hand off to the QoS 1 or QoS 2 engine for acknowledgement tracking. QoS 0 messages are sent
and forgotten immediately.

**Result:** The caller publishes a message with a single call regardless of QoS level. All framing,
identifier assignment, and acknowledgement waiting are handled internally. The caller is notified
when the delivery guarantee implied by the chosen QoS level has been met.

**Implementation status (2026-04-27): Completed (client-side module implemented)**

Implemented client-side publish pipeline:
- `src/client/publish_pipeline.h/.cpp`
	- validates outbound topic names,
	- builds outbound `PUBLISH` packets from caller messages,
	- assigns packet identifiers for QoS 1 and QoS 2,
	- tracks pending QoS handshakes by packet-id,
	- finalizes QoS 1 on `PUBACK`,
	- advances QoS 2 on `PUBREC` and emits outbound `PUBREL`,
	- finalizes QoS 2 on `PUBCOMP`.

Existing related reusable components:
- `src/qos/packet_id_manager.h/.cpp`
- `src/data_model/session/inflight_entry.h`
- `src/codec/packet/publish_codec.h/.cpp`
- `src/topic/topic_validator.h/.cpp`

Verification:
- `src/client/test/TEST_SPEC.md`
- `src/client/test/client_test.cpp`
	- qos0 immediate completion,
	- qos1 packet-id assignment and `PUBACK` completion,
	- qos2 `PUBREC` to `PUBREL` progression and `PUBCOMP` completion,
	- unknown packet-id and wrong-stage ACK error handling,
	- invalid topic validation handling.

---

### Step 22 – Reconnect Controller

Implement a component that monitors the connection state, detects drops (transport errors or
keep-alive timeout), and automatically re-establishes the connection. A configurable back-off
strategy controls the delay between attempts. After a successful reconnect the connection negotiator
(Step 18), the session state keeper (Step 19), and the QoS engines (Steps 14 and 15) are invoked
to restore the previous state.

**Result:** The client recovers from network interruptions without any action from the caller.
The caller's message callbacks and subscriptions remain active across reconnects. The back-off
strategy prevents the client from flooding the broker during outages.

**Implementation status (2026-04-27): Completed (client-side module implemented)**

Implemented client-side reconnect controller:
- `src/client/reconnect_controller.h/.cpp`
	- accepts disconnect triggers (`TransportError`, `KeepAliveTimeout`, `UserInitiated`),
	- schedules reconnect attempts using configurable backoff policy,
	- executes negotiation callback for reconnect attempts,
	- invokes session and QoS restore callbacks after successful reconnect,
	- exposes reconnect state, retry deadline, and last error for diagnostics.

Existing related reusable components:
- `src/client/connection_negotiator.h/.cpp`
- `src/client/session_state_keeper.h/.cpp`
- `src/client/publish_pipeline.h/.cpp`

Verification:
- `src/client/test/TEST_SPEC.md`
- `src/client/test/reconnect_controller_test.cpp`
	- transport and keep-alive trigger handling,
	- bounded backoff progression across failures,
	- successful reconnect reset behavior,
	- restore callback invocation,
	- user-initiated disconnect suppression.

---

## Phase 6 – Client Library Public API

### Step 23 – Synchronous Client Interface

Expose a blocking public interface with the following operations: connect, publish, subscribe,
unsubscribe, and disconnect. Each call blocks until the operation is complete or a timeout expires.
The interface hides all internal state machines, engines, and threading from the caller.

**Result:** Any application can use the library without knowledge of MQTT internals. Simple use
cases require no concurrency management on the caller's side.

**Implementation status (2026-04-27): Completed (client-side module implemented)**

Implemented synchronous client interface facade:
- `src/client_api/sync_client.h/.cpp`
	- exposes blocking operations: `connect`, `publish`, `subscribe`,
	  `unsubscribe`, `disconnect`,
	- composes lower-level client components (`ClientPublishPipeline`,
	  `ClientSubscriptionManager`, `ClientSessionStateKeeper`,
	  `ReconnectController`),
	- forwards per-operation timeout values to integration wait callbacks,
	- performs QoS-aware publish completion path (QoS0/1/2),
	- enforces connected-state preconditions for blocking operations.

Existing related reusable components:
- `src/client/publish_pipeline.h/.cpp`
- `src/client/subscription_manager.h/.cpp`
- `src/client/reconnect_controller.h/.cpp`

Verification:
- `src/client_api/test/TEST_SPEC.md`
- `src/client_api/test/sync_client_test.cpp`
	- connect state transition and timeout forwarding,
	- QoS0 and QoS2 blocking publish paths,
	- subscribe/unsubscribe blocking roundtrip behavior,
	- missing callback and disconnected-state error handling.

---

### Step 24 – Asynchronous / Callback Interface

Extend the public interface with non-blocking variants of connect, publish, subscribe, and
unsubscribe that accept a completion callback. Inbound messages are delivered via a registered
message handler callback. All callbacks are invoked from a single internal dispatch thread to
avoid concurrency issues for the caller.

**Result:** The library can be embedded in event-driven applications where blocking is not acceptable.
Callers choose between the blocking and the callback interface independently for each operation.

**Implementation status (2026-04-27): Completed (client-side module implemented)**

Implemented asynchronous callback interface facade:
- `src/client_api/async_client.h/.cpp`
	- exposes non-blocking operations: `async_connect`, `async_publish`,
	  `async_subscribe`, `async_unsubscribe`, `async_disconnect`,
	- wraps existing synchronous facade (`SyncClient`) and reuses transport
	  integration callbacks,
	- enqueues operations to one internal dispatch thread,
	- invokes completion callbacks and inbound message handler on that same
	  dispatch thread,
	- maps operation failures to `AsyncOperationError` callback payloads.

Related sync-facade extension:
- `src/client_api/sync_client.h/.cpp`
	- added `dispatch_inbound_publish(...)` to forward inbound publish packets
	  to active subscription callbacks.

Verification:
- `src/client_api/test/TEST_SPEC.md`
- `src/client_api/test/async_client_test.cpp`
	- callback-thread execution and non-blocking completion,
	- publish and subscribe/unsubscribe completion flows,
	- inbound publish forwarding through registered message handler,
	- completion error payload mapping.

---

### Step 25 – Configuration Object

Define a single configuration structure covering all tunable parameters: broker host and port,
transport type (TCP or WebSocket), client identifier, credentials, clean-start flag, keep-alive
interval, session expiry interval, receive maximum, topic alias maximum, reconnect policy, and
per-operation timeout. Sensible defaults are provided for every parameter.

**Result:** There is one authoritative place for all library settings. A minimal use case requires
only host and port; advanced use cases can tune every parameter without touching library internals.

**Implementation status (2026-04-27): Completed (client-side module implemented)**

Implemented unified client configuration object:
- `src/client_api/client_config.h/.cpp`
	- defines `ClientConfig` for transport target, credentials, CONNECT behavior,
	  reconnect policy, and per-operation timeouts,
	- provides defaults for all configuration fields,
	- validates configuration via `validate_client_config_or_throw(...)`,
	- builds CONNECT packet model via `build_connect_packet(...)`.

Integrated config object into public facades:
- `src/client_api/sync_client.h/.cpp`
	- supports construction from `ClientConfig`,
	- adds no-timeout overloads that use configured operation timeouts,
	- adds `connect()` overload using configured CONNECT model,
	- exposes current config through `client_config()`.
- `src/client_api/async_client.h/.cpp`
	- supports construction from `ClientConfig`,
	- adds no-timeout async overloads using configured operation timeouts,
	- adds `async_connect(...)` overload using configured CONNECT model,
	- exposes current config via thread-safe copy.

Verification:
- `src/client_api/test/TEST_SPEC.md`
- `src/client_api/test/client_config_test.cpp`
	- default value sanity and validation checks,
	- CONNECT packet mapping from config,
	- sync/async facade default-timeout integration.

---

### Step 26 – Error Model

Define a unified error type that covers: network errors, protocol violations, authentication
failures, authorization failures, broker-reported reason codes, and operation timeouts.
Every public function returns or signals an error of this type consistently.

**Result:** Callers always receive a structured, named error rather than a raw integer or an
unhandled exception. Error handling patterns are the same for every library function.

**Implementation status (2026-04-27): Completed (client-side module implemented)**

Implemented unified public client error model:
- `src/client_api/client_api_error.h/.cpp`
	- defines `ClientApiError` payload, `ClientApiErrorCategory`, and
	  `ClientApiException`,
	- maps internal `ClientException` values to public categories,
	- classifies broker reason codes into authentication/authorization/
	  protocol/broker categories,
	- maps generic standard exceptions to unknown-category public errors.

Integrated unified error signaling into public facades:
- `src/client_api/sync_client.h/.cpp`
	- all public operation failures are signaled through `ClientApiException`,
	- broker-reported ACK/CONNACK error reason codes are converted to
	  `ClientApiException` with populated reason code.
- `src/client_api/async_client.h/.cpp`
	- completion error payload uses unified `ClientApiError` type,
	- synchronous facade exceptions are forwarded consistently through callback
	  payload.

Verification:
- `src/client_api/test/TEST_SPEC.md`
- `src/client_api/test/client_api_error_test.cpp`
	- category mapping for internal and broker errors,
	- unknown-category mapping for generic exceptions,
	- end-to-end sync facade error signaling behavior.

---

## Phase 7 – Test Client

### Test Client Requirements (derived from mqttx CLI capabilities)

The test client should cover MQTT-focused capabilities that are available in
`mqttx` command-line usage and are relevant for broker validation workflows.
The following requirements intentionally exclude non-MQTT utility features
such as update checking.

- Support connection profiles for MQTT 5.0 only, including host, port,
	client ID, clean start/session behavior, keep alive, username, and password.
- Support only the broker transport variants `mqtt` and `ws`, including
	WebSocket path and optional WebSocket headers.
- No TLS support is required for the test client (no `mqtts`, no `wss`,
	no certificate/key/CA/ALPN options).
- Support reconnect policy controls: reconnect period and maximum reconnect
	attempts.
- Support MQTT 5 CONNECT properties and behavior controls, including session
	expiry interval, receive maximum, maximum packet size, topic alias maximum,
	response-information request, problem-information request toggle, and CONNECT
	user properties.
- Support MQTT 5 enhanced authentication method selection.
- Support Last Will configuration: topic, payload, QoS, retain, delay interval,
	payload format indicator, message expiry interval, content type, response
	topic, correlation data, and will user properties.
- Support one-shot publish operations with topic, payload, QoS, retain, DUP,
	and payload input modes (literal payload, stdin, multiline stdin, file input).
- Support publish message encodings and schema-based payload input paths needed
	for interoperability checks (for example JSON/hex/base64/binary, protobuf,
	and avro usage modes).
- Support MQTT 5 PUBLISH properties: payload format indicator, message expiry,
	topic alias, response topic, correlation data, user properties, subscription
	identifier, and content type.
- Support subscribe operations for one or more topics with per-subscription QoS
	and MQTT 5 subscription options (`no local`, `retain as published`,
	`retain handling`, subscription identifiers, user properties).
- Support subscriber output modes suitable for automation: verbose packet output,
	clean output mode, message formatting options, file append/save options, and
	configurable delimiters.
- Support benchmark-style test execution modes aligned with MQTT workflows:
	mass connection creation, high-rate publish runs, and multi-client subscribe
	runs.
- Support simulation/load scenarios with configurable connection count,
	connect interval, message interval, publish limit, variable-based topic/client
	templates, and QoS/retain/message-property controls.
- Support discovery of available built-in scenarios (equivalent to listing
	predefined simulation scenarios).
- Support loading and saving reusable command options/configuration profiles to
	improve reproducibility of manual and CI-driven test runs.

### Step 27 – Test Client Core, Connection Profiles, and Persistence

Build a standalone executable with subcommands that can load and save reusable option profiles from
configuration files and command-line arguments. The connection model is explicitly limited to MQTT 5.0
and broker-supported transports `mqtt` and `ws` (including WebSocket path and optional headers), with
no TLS features. The base connection profile includes host, port, client identifier, clean-start/session
behavior, keep-alive, username/password, and reconnect policy controls (retry period and maximum retry
count).

**Result:** A runnable test-client foundation exists that consistently reproduces connection setups,
matches the broker capability envelope (MQTT 5.0 + `mqtt`/`ws` only), and can be reused reliably in
manual and CI workflows.

**Implementation status (2026-04-27): Completed (Step 27 shell implemented)**

Implemented Step 27 shell module and executable:
- `src/test_client/test_client_profile.h/.cpp`
	- persistent profile model, key/value load/save, profile validation, and
		typed override application.
- `src/test_client/test_client_cli.h/.cpp`
	- CLI parser for `connect`, `save-profile`, and `show-profile`.
- `src/test_client_main.cpp`
	- standalone `yahatestclient` entry point, profile merge pipeline,
		connection-session orchestration, retry loop, and signal-controlled run
		mode.
- `CMakeLists.txt`
	- adds `yahatestclient` executable target and installation rule.

Verification:
- `src/test_client/test/TEST_SPEC.md`
- `src/test_client/test/test_client_test.cpp`
	- profile validation and persistence roundtrips,
	- profile parse error paths,
	- CLI command parsing and error handling.

---

### Step 28 – MQTT 5 CONNECT Feature Completeness

Extend connect handling to support MQTT 5-specific CONNECT configuration in full detail: session-expiry
interval, receive maximum, maximum packet size, topic-alias maximum, response-information request,
problem-information toggle, CONNECT user properties, enhanced authentication method, and full Last Will
configuration (topic, payload, QoS, retain, delay interval, payload format indicator, expiry interval,
content type, response topic, correlation data, and will user properties).

**Result:** The test client can establish protocol-rich MQTT 5 sessions that exercise broker behavior
across advanced CONNECT and Last Will negotiation paths, not only minimal handshake paths.

**Implementation status (2026-04-27): Completed (Step 28 connect envelope implemented)**

Implemented Step 28 in test-client shell:
- `src/test_client/test_client_profile.h/.cpp`
	- adds persistent keys and validation for CONNECT property controls,
		enhanced authentication options, and full Last Will configuration.
- `src/test_client/test_client_cli.cpp`
	- adds CLI flags for session/receive/packet-size/topic-alias controls,
		response/problem-info toggles, connect user properties, auth method/data,
		and will option set.
- `src/test_client_main.cpp`
	- builds CONNECT packet with additional MQTT 5 properties and optional will
		property block before negotiation (mqtt and ws paths).

Verification:
- `src/test_client/test/TEST_SPEC.md`
- `src/test_client/test/test_client_test.cpp`
	- covers extended profile roundtrip and validation for Step 28 fields,
	- covers CLI parsing paths for Step 28 options.

---

### Step 29 – Command-Line Publish Matrix

Implement a publish command that supports topic, QoS, retain, DUP, and all required MQTT 5 PUBLISH
properties (payload format indicator, message expiry, topic alias, response topic, correlation data,
user properties, subscription identifier, content type). Payload input modes include direct CLI payload,
stdin, multiline stdin, and file-based input. Add payload-encoding/schema options required for
interoperability checks, including JSON, hex, base64, binary, protobuf, and avro-driven flows.

**Result:** Single-message publish behavior can be validated end-to-end for wire flags, properties,
payload sources, and payload encodings, with deterministic success/failure exit signaling per QoS path.

**Implementation status (2026-04-27): Completed (Step 29 publish command implemented)**

Implemented Step 29 in test-client shell:
- `src/test_client/test_client_profile.h/.cpp`
	- adds publish command profile keys for topic/QoS/retain/dup,
		payload source modes, payload/correlation encodings, and PUBLISH
		property options.
- `src/test_client/test_client_cli.cpp`
	- adds `publish` command and corresponding CLI flags.
- `src/test_client_main.cpp`
	- connects using existing transport envelope, sends one PUBLISH,
		waits for QoS completion (`PUBACK` or `PUBREC`/`PUBCOMP`), and exits with
		clear success/failure behavior.

Verification:
- `src/test_client/test/TEST_SPEC.md`
- `src/test_client/test/test_client_test.cpp`
	- covers `publish` CLI parsing and extended profile persistence/validation
		used by publish matrix flows.

---

### Step 30 – Command-Line Subscribe and Output Pipeline

Implement subscribe command support for one or multiple topic filters with per-subscription QoS and
MQTT 5 subscription options (`no local`, `retain as published`, `retain handling`, subscription
identifiers, user properties). The output pipeline supports default and clean output modes, verbose
packet-level output, message formatting controls, file append/save sinks, and configurable delimiters
for scripting and post-processing.

**Result:** Incoming broker traffic can be observed and captured in both human-readable and
automation-friendly forms while exercising full MQTT 5 subscription-option behavior.

**Implementation status (2026-04-27): Completed (Step 30 subscribe command + output pipeline implemented)**

Implemented Step 30 in test-client shell:
- `src/test_client/test_client_profile.h/.cpp`
	- adds subscribe profile keys for per-subscription option entries,
		subscribe properties, and output-pipeline controls.
- `src/test_client/test_client_cli.h/.cpp`
	- adds `subscribe` command and corresponding CLI flags.
- `src/test_client_main.cpp`
	- sends SUBSCRIBE with MQTT 5 properties,
	- validates SUBACK outcomes,
	- streams inbound PUBLISH packets through configurable output modes,
	- acknowledges inbound QoS 1/2 publish flows.

Verification:
- `src/test_client/test/TEST_SPEC.md`
- `src/test_client/test/test_client_test.cpp`
	- covers subscribe profile persistence/validation,
	- covers subscribe CLI parsing.
- `test/integration_client_shell_cases.py`
- `test/integration_tests/client/test_client_shell.py`
	- covers local test-client publish-only cases and subscribe+publish roundtrip
		cases via consolidated client-shell integration test suite.

---

### Step 31 – Scenario Runner and Discoverable Scenario Catalog

Provide a scripted scenario runner that executes operation sequences (connect, subscribe, publish,
wait/assert message, unsubscribe, disconnect, sleep) with step-level pass/fail logging and non-zero
exit on failure. Add a discoverable built-in scenario catalog command so predefined protocol and
workload scenarios can be listed and selected directly. Built-in protocol scenarios cover clean start,
session resume, QoS 0/1/2 flows, retained replay, last-will behavior, keep-alive timeout handling,
topic-alias usage, and session-expiry behavior.

**Result:** Repeatable end-to-end validation is available as executable scenario assets that are easy
to discover, run, and integrate into existing integration pipelines.

**Implementation status (2026-04-27): Completed (Step 31 scenario runner + catalog implemented)**

Implemented Step 31 in test-client shell:
- `src/test_client/test_client_cli.h/.cpp`
	- adds `scenario` command parsing and Step 31 selectors
	  (`--scenario`, `--list-scenarios`).
- `src/test_client/test_client_scenario_runner.h/.cpp`
	- adds built-in scenario catalog API,
	- executes scripted scenario step chains with per-step pass/fail logging,
	- returns non-zero on first failed step,
	- supports catalog-list mode for discoverability.
- `src/test_client_main.cpp`
	- dispatches `scenario` command execution through the new runner module.

Verification:
- `src/test_client/test/TEST_SPEC.md`
- `src/test_client/test/test_client_test.cpp`
	- covers Step 31 CLI parsing and validation.
- `src/test_client/test/test_client_scenario_runner_test.cpp`
	- covers scenario catalog listing,
	- covers unknown-scenario rejection,
	- covers successful and failing scenario execution paths with mock executables.

---

### Step 32 – Benchmark and Simulation Load Modes

Implement load-oriented modes for (1) mass connection creation, (2) high-rate publish throughput, and
(3) multi-client subscribe behavior, plus simulation scenarios with configurable connection count,
connect interval, message interval, publish limit, variable-driven topic/client templates, QoS/retain,
and MQTT 5 message-property controls. Report throughput, latency, and failure/timeout counters in a
machine-consumable format aligned with current performance-test tooling.

**Result:** The test client can stress realistic broker paths at scale and generate comparable,
repeatable performance evidence using the same MQTT features exercised in functional scenarios.

**Implementation status (2026-04-27): Completed (Step 32 load modes + metrics implemented)**

Implemented Step 32 in test-client shell:
- `src/test_client/test_client_cli.h/.cpp`
	- adds Step 32 selector and tuning flags for `scenario` command:
	  - `--load-mode <mass-connect|publish-rate|multi-subscribe>`
	  - `--connection-count <count>`
	  - `--connect-interval-ms <milliseconds>`
	  - `--message-interval-ms <milliseconds>`
	  - `--publish-limit <count>`
	  - `--topic-template <template-with-{index}>`
	  - `--client-template <template-with-{index}>`
	  - `--metrics-json`
	- updates selector validation so `scenario` accepts one of:
	  `--scenario`, `--load-mode`, `--list-scenarios`.
- `src/test_client/test_client_scenario_runner.h/.cpp`
	- keeps Step 31 scripted scenarios,
	- adds Step 32 runtime modes:
	  - `mass-connect`: repeated connect/publish operations over generated client/topic pairs,
	  - `publish-rate`: high-rate publish loop with interval control,
	  - `multi-subscribe`: concurrent subscriber tasks + coordinated publish fanout,
	- adds metrics aggregation:
	  attempted/succeeded/failed/timed_out, duration, throughput, latency min/avg/max,
	- prints machine-consumable output line with `LOAD_METRICS_JSON {...}` when enabled.

Verification:
- `src/test_client/test/test_client_test.cpp`
	- covers Step 32 CLI option parsing for load-mode flags.
- `src/test_client/test/test_client_scenario_runner_test.cpp`
	- covers `mass-connect`, `publish-rate`, and `multi-subscribe` success paths,
	- covers unknown load-mode rejection path.
- Full project verification:
	- `python3 test/run_coverage.py`
	- `1226/1226` tests passed, coverage threshold met.

---

## Dependency Order Summary

| Phase | Steps | Depends on |
|-------|-------|------------|
| 1 – Primitives | 1–4 | nothing |
| 2 – Codec | 5–10 | Phase 1 |
| 3 – Transport | 11–12 | Phase 1 |
| 4 – Protocol Engine | 13–17 | Phase 2, Phase 3 |
| 5 – Session Logic | 18–22 | Phase 4 |
| 6 – Public API | 23–26 | Phase 5 |
| 7 – Test Client | 27–32 | Phase 6 |

Each step within a phase may be implemented in any order unless a later step in the same phase
explicitly uses the result of an earlier one.
