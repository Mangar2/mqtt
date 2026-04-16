# TEST_SPEC — fixed header codec (Module 2.3)

## encode_fixed_header

| Test name                        | Scenario                                              | Input                                      | Expected wire bytes |
|----------------------------------|-------------------------------------------------------|--------------------------------------------|---------------------|
| `fh_encode_connect`              | CONNECT, flags=0, remaining=10                        | `{Connect, 0, 10}`                         | `[0x10, 0x0A]` |
| `fh_encode_publish_flags`        | PUBLISH with DUP+QoS1 flags (0x0A), remaining=20      | `{Publish, 0x0A, 20}`                      | `[0x3A, 0x14]` |
| `fh_encode_subscribe`            | SUBSCRIBE, flags=0x02, remaining=0                    | `{Subscribe, 0x02, 0}`                     | `[0x82, 0x00]` |
| `fh_encode_large_remaining`      | Remaining length requiring multi-byte VBI             | `{Connack, 0, 128}`                        | `[0x20, 0x80, 0x01]` |
| `fh_encode_max_remaining`        | Remaining = 268 435 455 (VBI maximum)                 | `{Publish, 0, 268435455}`                  | 5-byte header |

## decode_fixed_header

| Test name                        | Scenario                                              | Input bytes                | Expected |
|----------------------------------|-------------------------------------------------------|----------------------------|----------|
| `fh_decode_connect`              | CONNECT fixed header                                  | `[0x10, 0x0A]`             | type=Connect, flags=0, remaining=10 |
| `fh_decode_publish_flags`        | PUBLISH with flags byte                               | `[0x3A, 0x14]`             | type=Publish, flags=0x0A, remaining=20 |
| `fh_decode_subscribe`            | SUBSCRIBE with flags=0x02                             | `[0x82, 0x00]`             | type=Subscribe, flags=0x02, remaining=0 |
| `fh_decode_pubrel`               | PUBREL with flags=0x02                                | `[0x62, 0x00]`             | type=Pubrel, flags=0x02, remaining=0 |
| `fh_decode_unsubscribe`          | UNSUBSCRIBE with flags=0x02                           | `[0xA2, 0x00]`             | type=Unsubscribe, flags=0x02, remaining=0 |
| `fh_decode_multi_byte_remaining` | Remaining length is a 2-byte VBI                      | `[0x20, 0x80, 0x01]`       | type=Connack, remaining=128 |
| `fh_decode_type_0_reserved`      | Type nibble = 0 → throws                              | `[0x00, 0x00]`             | `CodecError::InvalidPacketType` |
| `fh_decode_connect_bad_flags`    | CONNECT with non-zero flags → throws                  | `[0x11, 0x00]`             | `CodecError::InvalidFlags` |
| `fh_decode_subscribe_bad_flags`  | SUBSCRIBE with flags=0x01 → throws                    | `[0x81, 0x00]`             | `CodecError::InvalidFlags` |
| `fh_decode_pubrel_bad_flags`     | PUBREL with flags=0x00 → throws                       | `[0x60, 0x00]`             | `CodecError::InvalidFlags` |
| `fh_decode_truncated_no_remaining`| Only 1 byte (no remaining length) → throws           | `[0x10]`                   | `CodecError::BufferTooShort` |
| `fh_decode_empty`                | Empty buffer → throws                                 | `[]`                       | `CodecError::BufferTooShort` |
| `fh_decode_vbi_overflow`         | Malformed VBI in remaining length → throws            | `[0x10, 0xFF, 0xFF, 0xFF, 0xFF]` | `CodecError::VariableByteIntegerOverflow` |

## Round-trip

| Test name         | Scenario                                           | Expected |
|-------------------|----------------------------------------------------|----------|
| `fh_roundtrip`    | encode then decode gives back identical FixedHeader| same FixedHeader |
