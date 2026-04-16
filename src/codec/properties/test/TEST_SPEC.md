# TEST_SPEC — properties codec (Module 2.2)

## encode_properties

| Test name                         | Scenario                                              | Expected |
|-----------------------------------|-------------------------------------------------------|----------|
| `props_encode_empty`              | Empty property list → `[0x00]`                        | `[0x00]` |
| `props_encode_byte_property`      | Single Byte property encodes correctly                | ID + value bytes, VBI length prefix |
| `props_encode_two_byte_property`  | Single TwoByteInteger property encodes correctly      | correct wire bytes |
| `props_encode_four_byte_property` | Single FourByteInteger property                       | correct wire bytes |
| `props_encode_vbi_property`       | Single VariableByteInteger property                   | correct wire bytes |
| `props_encode_utf8_property`      | Single Utf8String property                            | correct wire bytes |
| `props_encode_binary_property`    | Single BinaryData property                            | correct wire bytes |
| `props_encode_multiple`           | Multiple properties of different types                | all properties encoded in order |
| `props_encode_type_mismatch`      | Property value type doesn't match expected for ID     | throws `PropertyTypeMismatch` |
| `props_encode_not_allowed`        | Property not permitted in context                     | throws `PropertyNotAllowed` |
| `props_encode_duplicate`          | Same non-UserProperty ID appears twice                | throws `DuplicateProperty` |
| `props_encode_user_prop_repeat`   | UserProperty appears twice — should succeed           | encoded successfully |

## decode_properties

| Test name                         | Scenario                                              | Expected |
|-----------------------------------|-------------------------------------------------------|----------|
| `props_decode_empty`              | `[0x00]` → empty vector                               | `{}` |
| `props_decode_single`             | One property                                          | vector of one Property |
| `props_decode_multiple`           | Several properties of mixed types                     | all decoded correctly |
| `props_decode_invalid_id`         | Unknown property ID byte                              | throws `InvalidPropertyId` |
| `props_decode_not_allowed`        | Property not permitted in context                     | throws `PropertyNotAllowed` |
| `props_decode_duplicate`          | Non-repeatable property appears twice in wire data    | throws `DuplicateProperty` |
| `props_decode_truncated`          | Properties length claims N bytes but buffer shorter   | throws `BufferTooShort` |
| `props_decode_user_prop_repeat`          | UserProperty appears twice — should decode both                        | two UserProperty entries |
| `props_decode_remaining_property_ids`   | Roundtrip decode for all property IDs not exercised by other tests     | correct IDs decoded per context (Connack, Connect, Publish, Will) |

## Round-trip

| Test name              | Scenario                                        | Expected |
|------------------------|-------------------------------------------------|----------|
| `props_roundtrip`      | encode then decode gives back identical vector  | same Properties |
