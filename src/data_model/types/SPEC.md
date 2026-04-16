# types — Module 1.1: Primitive Types

Definitions of every MQTT 5.0 wire-level primitive type (Section 1.5 of the spec).

## Files

| File                       | Contents |
|----------------------------|----------|
| `variable_byte_integer.h`  | `VariableByteInteger` struct (1–4 byte encoding, 0..268 435 455) |
| `utf8_string.h`            | `Utf8String`, `Utf8StringPair` structs |
| `binary_data.h`            | `BinaryData` struct |
| `integers.h`               | `TwoByteInteger` (uint16_t alias), `FourByteInteger` (uint32_t alias) |
| `qos.h`                    | `QoS` enum (AtMostOnce / AtLeastOnce / ExactlyOnce) |

## Public API

### VariableByteInteger
- `uint32_t value` — raw value; valid range [0, k_max_value].
- `static constexpr uint32_t k_max_value = 268'435'455` — maximum encodable value.
- `[[nodiscard]] constexpr uint8_t encoded_size() const noexcept` — returns 1–4 (bytes needed on the wire).
- `operator==` defaulted.

### Utf8String
- `std::string value` — content must be valid UTF-8; max `k_max_byte_length` bytes.
- `static constexpr std::size_t k_max_byte_length = 65535` — MQTT length-prefix limit.
- `operator==` defaulted.

### Utf8StringPair
- `Utf8String name`, `Utf8String value`.
- `operator==` defaulted.

### BinaryData
- `std::vector<uint8_t> data` — max `k_max_byte_length` bytes.
- `static constexpr std::size_t k_max_byte_length = 65535`.
- `operator==` defaulted.

### TwoByteInteger / FourByteInteger
- Type aliases for `uint16_t` / `uint32_t`. Big-endian encoding is handled by the codec module.

### QoS
- `enum class QoS : uint8_t { AtMostOnce = 0, AtLeastOnce = 1, ExactlyOnce = 2 }`.
