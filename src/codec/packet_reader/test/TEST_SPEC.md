# TEST_SPEC — codec/packet_reader

Unit tests for Module 2.13: Packet Reader (`read_packet`).

---

## packet_reader_test.cpp

### Dispatching — one round-trip per packet type

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `read_packet_connect` | Full CONNECT round-trip via `read_packet` | encode_connect → read_packet | returns `ConnectPacket`, values match |
| `read_packet_connack` | Full CONNACK round-trip via `read_packet` | encode_connack → read_packet | returns `ConnackPacket`, values match |
| `read_packet_publish_qos0` | QoS-0 PUBLISH round-trip | encode_publish (QoS=0) → read_packet | returns `PublishPacket`, topic/payload match |
| `read_packet_publish_qos2` | QoS-2 PUBLISH round-trip (flags forwarded) | encode_publish (QoS=2) → read_packet | returns `PublishPacket`, qos=2, packet_id present |
| `read_packet_puback` | PUBACK round-trip | encode_puback → read_packet | returns `PubackPacket` |
| `read_packet_pubrec` | PUBREC round-trip | encode_pubrec → read_packet | returns `PubrecPacket` |
| `read_packet_pubrel` | PUBREL round-trip | encode_pubrel → read_packet | returns `PubrelPacket` |
| `read_packet_pubcomp` | PUBCOMP round-trip | encode_pubcomp → read_packet | returns `PubcompPacket` |
| `read_packet_subscribe` | SUBSCRIBE round-trip | encode_subscribe → read_packet | returns `SubscribePacket` |
| `read_packet_suback` | SUBACK round-trip | encode_suback → read_packet | returns `SubackPacket` |
| `read_packet_unsubscribe` | UNSUBSCRIBE round-trip | encode_unsubscribe → read_packet | returns `UnsubscribePacket` |
| `read_packet_unsuback` | UNSUBACK round-trip | encode_unsuback → read_packet | returns `UnsubackPacket` |
| `read_packet_pingreq` | PINGREQ round-trip | encode_pingreq → read_packet | returns `PingreqPacket` |
| `read_packet_pingresp` | PINGRESP round-trip | encode_pingresp → read_packet | returns `PingrespPacket` |
| `read_packet_disconnect` | DISCONNECT round-trip | encode_disconnect → read_packet | returns `DisconnectPacket` |
| `read_packet_auth` | AUTH round-trip | encode_auth → read_packet | returns `AuthPacket` |

### Buffer advancement

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `read_packet_advances_cursor` | Two packets back-to-back in one buffer | encode two PINGREQ packets concatenated | two successful reads, no bytes remaining |

### Error cases

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `read_packet_empty_buffer` | Buffer is empty | zero bytes | CodecError::BufferTooShort |
| `read_packet_truncated_payload` | Buffer shorter than declared remaining_length | only Fixed Header, no payload | CodecError::BufferTooShort |
| `read_packet_reserved_type_zero` | First byte encodes type nibble == 0 | 0x00 0x00 | CodecError::InvalidPacketType |
| `codec_exception_detected_protocol_version_roundtrip` | CodecException stores detected protocol version when provided | construct `CodecException(InvalidProtocolVersion, ..., 0x04)` | `detected_protocol_version()==0x04`, `error()==InvalidProtocolVersion` |
| `codec_exception_detected_protocol_version_default_nullopt` | CodecException protocol version is empty by default | construct `CodecException(BufferTooShort, ...)` | `detected_protocol_version()==nullopt`, `what()` preserved |
