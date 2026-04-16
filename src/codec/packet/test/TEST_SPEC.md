# TEST_SPEC — codec/packet

Unit tests for Modules 2.4–2.12: CONNECT, CONNACK, PUBLISH, PUBACK, PUBREC,
PUBREL, PUBCOMP, SUBSCRIBE, SUBACK, UNSUBSCRIBE, UNSUBACK, PINGREQ, PINGRESP,
DISCONNECT, AUTH codecs.

---

## connect_codec_test.cpp

### CONNECT encode

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `connect_encode_minimal` | Minimal CONNECT (client_id only, no will, no user/pass, clean_start=true, keep_alive=0) | Default ConnectPacket with client_id="test" | Fixed header 0x10, correct wire bytes |
| `connect_encode_keep_alive` | CONNECT with keep_alive=60 | keep_alive=60 | Bytes [0x00,0x3C] in variable header |
| `connect_encode_clean_start_false` | clean_start=false clears bit 1 | clean_start=false | Connect Flags bit 1 == 0 |
| `connect_encode_will_qos0` | CONNECT with Will, QoS=0, retain=false | will with QoS=0 | Will Flag bit set, QoS bits=00 |
| `connect_encode_will_qos2_retain` | CONNECT with Will, QoS=2, retain=true | will with QoS=2, retain=true | Will QoS bits=10, Will Retain bit set |
| `connect_encode_username` | CONNECT with username | username="user" | Username Flag bit set, username in payload |
| `connect_encode_password` | CONNECT with username + password | username + password | Both flags set |
| `connect_encode_roundtrip` | Encode then decode matches original | Full ConnectPacket | decoded == original |

### CONNECT decode

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `connect_decode_bad_protocol_name` | Protocol name not "MQTT" | "MQTX" | CodecError::InvalidProtocolName |
| `connect_decode_bad_protocol_version` | Version not 5 | level=4 | CodecError::InvalidProtocolVersion |
| `connect_decode_reserved_bit_set` | Connect Flags bit 0 == 1 | flags with bit0=1 | CodecError::MalformedPacket |
| `connect_decode_will_qos3` | Will QoS bits = 3 | flags with will_qos=3 | CodecError::InvalidQoS |
| `connect_decode_will_flags_without_will_flag` | Will Retain set but Will Flag not set | flags with retain=1, will=0 | CodecError::MalformedPacket |
| `connect_decode_truncated` | Buffer too short | fewer bytes than required | CodecError::BufferTooShort |

### CONNACK encode

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `connack_encode_success_no_session` | Clean connack | session_present=false, reason_code=Success | [0x20, len, 0x00, 0x00, 0x00] |
| `connack_encode_session_present` | Session resumed | session_present=true | ack_flags byte == 0x01 |
| `connack_encode_roundtrip` | Encode then decode | ConnackPacket | decoded == original |

### CONNACK decode

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `connack_decode_reserved_bits` | Ack Flags bits 7-1 are non-zero | ack_flags=0x02 | CodecError::MalformedPacket |

---

## publish_codec_test.cpp

### PUBLISH encode

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `publish_encode_qos0` | QoS=0, no packet_id | topic="a/b", payload="hello" | Fixed header 0x30, no packet_id |
| `publish_encode_qos1` | QoS=1 with packet_id=1 | qos=1, packet_id=1 | Fixed header 0x32, packet_id in wire |
| `publish_encode_qos2` | QoS=2 with packet_id=5 | qos=2, packet_id=5 | Fixed header 0x34, packet_id in wire |
| `publish_encode_dup_retain` | DUP=true, RETAIN=true, QoS=1 | dup=true, retain=true, qos=1 | flags byte = 0x0B |
| `publish_encode_missing_packet_id` | QoS=1 but no packet_id | qos=1, no packet_id | CodecError::MalformedPacket |
| `publish_encode_unexpected_packet_id` | QoS=0 but packet_id present | qos=0, packet_id=1 | CodecError::MalformedPacket |
| `publish_encode_roundtrip` | Encode then decode | PublishPacket QoS2 | decoded == original |

### PUBLISH decode

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `publish_decode_qos3` | QoS bits = 3 | flags=0x06 | CodecError::InvalidQoS |
| `publish_decode_dup_qos0` | DUP=1 with QoS=0 | flags=0x08 | CodecError::MalformedPacket |
| `publish_decode_empty_payload` | No payload bytes | QoS=0, topic, no payload | payload.data is empty |

### ACK packets (PUBACK, PUBREC, PUBREL, PUBCOMP)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `puback_roundtrip_short` | Success + empty props → short form | PubackPacket default | remaining_length==2 |
| `puback_roundtrip_full` | Non-success reason code | reason_code=NoMatchingSubscribers | remaining_length>2 |
| `pubrec_roundtrip` | Encode/decode PUBREC | PubrecPacket | decoded == original |
| `pubrel_fixed_flags` | PUBREL uses flags=0x02 | PubrelPacket | wire byte[0]==0x62 |
| `pubrel_roundtrip` | Encode/decode PUBREL | PubrelPacket | decoded == original |
| `pubcomp_roundtrip` | Encode/decode PUBCOMP | PubcompPacket | decoded == original |
| `puback_decode_short_form` | remaining_length==2 gives defaults | 2-byte buffer | reason_code=Success, no props |

---

## subscribe_codec_test.cpp

### SUBSCRIBE encode/decode

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `subscribe_encode_single_filter` | One filter, all default options | filter "a/b", QoS=1 | Single filter with options byte |
| `subscribe_encode_all_options` | No-local, retain-as-published, retain-handling=1 | full options | Options byte = 0x1E |
| `subscribe_encode_empty_filters` | No filters | empty filters vector | CodecError::MalformedPacket |
| `subscribe_fixed_flags` | Fixed header must have flags=0x02 | any subscribe | wire byte[0] == 0x82 |
| `subscribe_decode_qos3` | Options byte QoS=3 | options=0x03 | CodecError::InvalidQoS |
| `subscribe_decode_reserved_bits` | Options bits 7-6 non-zero | options=0x80 | CodecError::MalformedPacket |
| `subscribe_decode_invalid_retain_handling` | Retain handling=3 | options=0x30 | CodecError::MalformedPacket |
| `subscribe_roundtrip` | Encode/decode | SubscribePacket | decoded == original |

### SUBACK encode/decode

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `suback_encode_decode` | Multiple reason codes | [GrantedQoS1, GrantedQoS2] | decoded == original |
| `suback_roundtrip` | Encode/decode | SubackPacket | decoded == original |

### UNSUBSCRIBE encode/decode

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `unsubscribe_encode_single` | One topic filter | "a/b" | Single filter in payload |
| `unsubscribe_encode_empty` | No filters | empty | CodecError::MalformedPacket |
| `unsubscribe_fixed_flags` | Fixed header flags=0x02 | any | wire byte[0] == 0xA2 |
| `unsubscribe_roundtrip` | Encode/decode | UnsubscribePacket | decoded == original |

### UNSUBACK encode/decode

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `unsuback_roundtrip` | Encode/decode | UnsubackPacket with reason codes | decoded == original |

---

## control_codec_test.cpp

### PINGREQ / PINGRESP

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `pingreq_encode` | Encode PINGREQ | — | [0xC0, 0x00] |
| `pingresp_encode` | Encode PINGRESP | — | [0xD0, 0x00] |
| `pingreq_decode_empty` | Decode with empty buffer | 0-byte buffer | PingreqPacket{} |
| `pingresp_decode_empty` | Decode with empty buffer | 0-byte buffer | PingrespPacket{} |
| `pingreq_decode_non_empty` | Remaining bytes present | 1-byte buffer | CodecError::MalformedPacket |

### DISCONNECT

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `disconnect_encode_short_form` | Success + no props → length 0 | default DisconnectPacket | [0xE0, 0x00] |
| `disconnect_encode_with_reason` | Non-success reason code | reason_code=UnspecifiedError | remaining_length > 0 |
| `disconnect_roundtrip` | Encode/decode | DisconnectPacket with props | decoded == original |
| `disconnect_decode_short_form` | Empty buffer | 0-byte buffer | reason_code=Success, no props |
| `disconnect_decode_reason_only` | 1-byte buffer | [0x80] | reason_code=UnspecifiedError, no props |

### AUTH

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `auth_encode_short_form` | Success + no props | default AuthPacket | [0xF0, 0x00] |
| `auth_encode_continue` | ContinueAuthentication | reason_code=ContinueAuthentication | wire contains 0x18 |
| `auth_encode_invalid_reason_code` | Bad reason code | reason_code=MalformedPacket(0x81) | CodecError::MalformedPacket |
| `auth_decode_invalid_reason_code` | Decode bad reason code | [0x81] | CodecError::MalformedPacket |
| `auth_roundtrip` | Encode/decode | AuthPacket with props | decoded == original |
