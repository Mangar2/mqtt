# TEST_SPEC — primitive codec (Module 2.1)

## encode_byte / decode_byte

| Test name              | Scenario                         | Input              | Expected |
|------------------------|----------------------------------|--------------------|----------|
| `byte_encode`          | Encodes a single byte            | `0x42`             | `[0x42]` |
| `byte_decode`          | Decodes a single byte            | `[0xAB]`           | `0xAB`   |
| `byte_decode_empty`    | Buffer empty → throws            | `[]`               | `CodecError::BufferTooShort` |

## encode/decode Variable Byte Integer

| Test name                | Scenario                                        | Input / Value           | Expected |
|--------------------------|-------------------------------------------------|-------------------------|----------|
| `vbi_encode_1byte`       | Values 0 and 127 encode to 1 byte               | 0, 127                  | `[0x00]`, `[0x7F]` |
| `vbi_encode_2byte`       | Values 128 and 16 383 encode to 2 bytes         | 128, 16 383             | `[0x80,0x01]`, `[0xFF,0x7F]` |
| `vbi_encode_3byte`       | Values 16 384 and 2 097 151 encode to 3 bytes   | 16 384, 2 097 151       | `[0x80,0x80,0x01]`, `[0xFF,0xFF,0x7F]` |
| `vbi_encode_4byte`       | Values 2 097 152 and 268 435 455 encode to 4 bytes | 2 097 152, 268 435 455 | `[0x80,0x80,0x80,0x01]`, `[0xFF,0xFF,0xFF,0x7F]` |
| `vbi_encode_overflow`    | Value > max → throws                            | 268 435 456             | `CodecError::VariableByteIntegerOverflow` |
| `vbi_decode_1byte`       | Decodes 1-byte VBI values                       | `[0x00]`, `[0x7F]`      | 0, 127 |
| `vbi_decode_2byte`       | Decodes 2-byte VBI values                       | `[0x80,0x01]`, `[0xFF,0x7F]` | 128, 16 383 |
| `vbi_decode_3byte`       | Decodes 3-byte VBI                              | `[0x80,0x80,0x01]`      | 16 384 |
| `vbi_decode_4byte`       | Decodes maximum 4-byte VBI                      | `[0xFF,0xFF,0xFF,0x7F]` | 268 435 455 |
| `vbi_decode_truncated`   | Continuation bit set but no next byte → throws  | `[0x80]`                | `CodecError::BufferTooShort` |
| `vbi_decode_overflow`    | Continuation bit in 4th byte → throws           | `[0xFF,0xFF,0xFF,0xFF]` | `CodecError::VariableByteIntegerOverflow` |
| `vbi_roundtrip`          | Encode then decode round-trips correctly        | several values          | same value |

## encode/decode Two Byte Integer

| Test name                   | Scenario                                | Input   | Expected |
|-----------------------------|-----------------------------------------|---------|----------|
| `two_byte_encode`           | Big-endian encoding                     | 0x1234  | `[0x12,0x34]` |
| `two_byte_decode`           | Big-endian decoding                     | `[0xBE,0xEF]` | 0xBEEF |
| `two_byte_decode_truncated` | Only 1 byte available → throws          | `[0x12]` | `CodecError::BufferTooShort` |
| `two_byte_roundtrip`        | Encode then decode gives same value     | 0xDEAD  | 0xDEAD |

## encode/decode Four Byte Integer

| Test name                    | Scenario                            | Input       | Expected |
|------------------------------|-------------------------------------|-------------|----------|
| `four_byte_encode`           | Big-endian encoding                 | 0x12345678  | `[0x12,0x34,0x56,0x78]` |
| `four_byte_decode`           | Big-endian decoding                 | `[0xDE,0xAD,0xBE,0xEF]` | 0xDEADBEEF |
| `four_byte_decode_truncated` | Only 3 bytes available → throws     | `[0x12,0x34,0x56]` | `CodecError::BufferTooShort` |
| `four_byte_roundtrip`        | Encode then decode gives same value | 0xCAFEBABE  | 0xCAFEBABE |

## encode/decode UTF-8 String

| Test name                 | Scenario                                      | Input           | Expected |
|---------------------------|-----------------------------------------------|-----------------|----------|
| `utf8_encode_empty`       | Empty string → 2-byte zero length prefix      | `""`            | `[0x00,0x00]` |
| `utf8_encode`             | Non-empty string includes length prefix       | `"hello"`       | `[0x00,0x05,'h','e','l','l','o']` |
| `utf8_decode_empty`       | Decodes zero-length string                    | `[0x00,0x00]`   | `""` |
| `utf8_decode`             | Decodes non-empty string                      | `[0x00,0x03,'f','o','o']` | `"foo"` |
| `utf8_decode_multibyte_valid` | Decodes valid multi-byte UTF-8 sequences | bytes for `"\u20AC\U0001F642"` | exact decoded string |
| `utf8_decode_truncated`   | Length prefix says N but fewer bytes remain → throws | partial buffer | `CodecError::BufferTooShort` |
| `utf8_decode_forbidden_null` | U+0000 in content is rejected             | `[0x00,0x01,0x00]` | `CodecError::MalformedPacket` |
| `utf8_decode_invalid_leading_byte` | Invalid leading byte is rejected     | `[0x00,0x01,0x80]` | `CodecError::MalformedPacket` |
| `utf8_decode_invalid_continuation` | Missing/invalid continuation byte is rejected | malformed 2-byte sequence | `CodecError::MalformedPacket` |
| `utf8_decode_truncated_multibyte` | Multi-byte sequence truncated at end is rejected | incomplete 3-byte sequence | `CodecError::MalformedPacket` |
| `utf8_roundtrip`          | Encode then decode gives same value           | `"test"`        | `"test"` |

## encode/decode UTF-8 String Pair

| Test name             | Scenario                         | Input                  | Expected |
|-----------------------|----------------------------------|------------------------|----------|
| `utf8_pair_roundtrip` | Encode then decode a string pair | name="key", value="val"| same pair |

## encode/decode Binary Data

| Test name                 | Scenario                                   | Input              | Expected |
|---------------------------|--------------------------------------------|--------------------|----------|
| `binary_encode_empty`     | Empty data → 2-byte zero length prefix     | `{}`               | `[0x00,0x00]` |
| `binary_encode`           | Non-empty data includes length prefix      | `{0x01,0x02,0x03}` | `[0x00,0x03,0x01,0x02,0x03]` |
| `binary_decode`           | Decodes correctly                          | `[0x00,0x02,0xAB,0xCD]` | `{0xAB,0xCD}` |
| `binary_decode_truncated` | Fewer bytes than length prefix → throws    | partial buffer     | `CodecError::BufferTooShort` |
| `binary_roundtrip`        | Encode then decode gives same value        | `{0xDE,0xAD}`      | `{0xDE,0xAD}` |
