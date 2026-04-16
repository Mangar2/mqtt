# primitive — Module 2.1: Primitive Type Codec

Encodes and decodes every MQTT 5.0 wire-level primitive type (Section 1.5 of the spec).

## Files

| File                  | Contents |
|-----------------------|----------|
| `primitive_codec.h`   | Declarations of all encode / decode free functions |
| `primitive_codec.cpp` | Implementations |

## Public API

### Encoding (append to `WriteBuffer&`)

| Function                                            | Output bytes |
|-----------------------------------------------------|-------------|
| `encode_byte(buf, uint8_t)`                         | 1 |
| `encode_variable_byte_integer(buf, VariableByteInteger)` | 1–4 |
| `encode_two_byte_integer(buf, TwoByteInteger)`      | 2 (big-endian) |
| `encode_four_byte_integer(buf, FourByteInteger)`    | 4 (big-endian) |
| `encode_utf8_string(buf, Utf8String)`               | 2 + len |
| `encode_utf8_string_pair(buf, Utf8StringPair)`      | 2 + name.len + 2 + value.len |
| `encode_binary_data(buf, BinaryData)`               | 2 + len |

### Decoding (consume from `ReadBuffer&`)

| Function                              | Returns               |
|---------------------------------------|-----------------------|
| `decode_byte(buf)`                    | `uint8_t`             |
| `decode_variable_byte_integer(buf)`   | `VariableByteInteger` |
| `decode_two_byte_integer(buf)`        | `TwoByteInteger`      |
| `decode_four_byte_integer(buf)`       | `FourByteInteger`     |
| `decode_utf8_string(buf)`             | `Utf8String`          |
| `decode_utf8_string_pair(buf)`        | `Utf8StringPair`      |
| `decode_binary_data(buf)`             | `BinaryData`          |

## Error behaviour

| Error                       | Condition |
|-----------------------------|-----------|
| `BufferTooShort`            | `ReadBuffer` runs out of bytes mid-decode |
| `StringTooLong`             | UTF-8 string or binary data length > 65 535 |
| `VariableByteIntegerOverflow` | VBI value > 268 435 455, or continuation bit in 4th byte |
