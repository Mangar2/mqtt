# fixed_header — Module 2.3: Fixed Header Codec

Encodes and decodes the MQTT 5.0 Fixed Header (Section 2.1 of the spec).

## Files

| File                      | Contents |
|---------------------------|----------|
| `fixed_header.h`          | `FixedHeader` struct |
| `fixed_header_codec.h`    | Declarations of encode / decode free functions |
| `fixed_header_codec.cpp`  | Implementations |

## Wire format

```
Byte 1: [Packet Type (4 bits)] [Flags (4 bits)]
Bytes 2–5: Remaining Length (Variable Byte Integer, 1–4 bytes)
```

Total header size: 2–5 bytes.

## FixedHeader struct

```cpp
struct FixedHeader {
    PacketType type;
    uint8_t    flags;
    uint32_t   remaining_length;
};
```

## Public API

```cpp
void encode_fixed_header(WriteBuffer& buf, const FixedHeader& header);
[[nodiscard]] FixedHeader decode_fixed_header(ReadBuffer& buf);
```

## Flag constraints (2.3.1 / 2.3.2)

| Packet type                  | Required flags |
|------------------------------|----------------|
| PUBREL, SUBSCRIBE, UNSUBSCRIBE | `0x02`        |
| All others except PUBLISH    | `0x00`         |
| PUBLISH                      | Any (DUP \| QoS \| RETAIN); QoS=3 validated by PUBLISH codec |

## Error behaviour

| Error                         | Condition |
|-------------------------------|-----------|
| `BufferTooShort`              | Input truncated |
| `InvalidPacketType`           | Type nibble is 0 (reserved) |
| `InvalidFlags`                | Reserved flags are non-zero |
| `VariableByteIntegerOverflow` | Remaining-length VBI malformed |
