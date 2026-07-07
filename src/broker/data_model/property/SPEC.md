# property — Module 1.3: Property Definitions

All 27 MQTT 5.0 property identifiers, their data types, and the packet contexts in which
they are permitted (MQTT 5.0 Section 2.2).

## Files

| File               | Contents |
|--------------------|----------|
| `property_id.h`    | `PropertyId` enum — all 27 identifiers (1.3.1) |
| `property.h`       | `PropertyValue` variant, `Property` struct |
| `property_maps.h`  | `PropertyDataType` enum, `property_data_type()`, `is_property_allowed()` (1.3.2 / 1.3.3) |

## Public API

### PropertyId (enum class : uint8_t)
One enumerator per MQTT 5.0 property identifier (27 total).

### PropertyValue (std::variant)
Union of all possible property value types:
`uint8_t` | `TwoByteInteger` | `FourByteInteger` | `VariableByteInteger`
| `Utf8String` | `Utf8StringPair` | `BinaryData`

### Property
```cpp
struct Property {
    PropertyId    id;
    PropertyValue value;
    bool operator==(const Property&) const noexcept = default;
};
```

### PropertyDataType (enum class : uint8_t)
```
Byte | TwoByteInteger | FourByteInteger | VariableByteInteger | Utf8String | Utf8StringPair | BinaryData
```

### property_data_type(PropertyId) → PropertyDataType
Constexpr mapping from property identifier to its MQTT-specified data type.

### is_property_allowed(PropertyId, PacketType) → bool
Constexpr check whether a property is permitted in the given packet context.
`PacketType::Will` refers to the Will Properties block inside a CONNECT packet.
