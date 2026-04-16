# properties — Module 2.2: Properties Codec

Encodes and decodes the MQTT 5.0 properties section (Section 2.2.2 of the spec).
Validation covers: property type correctness, packet-type allowability, and duplicate detection.

## Files

| File                    | Contents |
|-------------------------|----------|
| `properties_codec.h`    | Declarations of encode / decode free functions |
| `properties_codec.cpp`  | Implementations (includes internal helpers in anonymous namespace) |

## Wire format

```
[Properties Length : VBI] [Property ID : 1 byte] [Value : type-dependent] ...
```

A length of 0 means no properties; the encoder still emits the `0x00` length byte.

## Public API

```cpp
void encode_properties(WriteBuffer& buf,
                       const std::vector<Property>& props,
                       PacketType context);

[[nodiscard]]
std::vector<Property> decode_properties(ReadBuffer& buf, PacketType context);
```

## Validation (2.2.3)

| Check              | When applied | Error thrown |
|--------------------|-------------|--------------|
| Type match         | Encode       | `PropertyTypeMismatch` |
| Allowed in packet  | Encode + Decode | `PropertyNotAllowed` |
| No duplicates (non-UserProperty) | Encode + Decode | `DuplicateProperty` |
| Known property ID  | Decode       | `InvalidPropertyId` |

`UserProperty` (0x26) may appear any number of times in a single properties section.
All other property IDs must appear at most once.
