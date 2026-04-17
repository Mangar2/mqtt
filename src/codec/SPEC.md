# codec — Module 2 Overview

Serialization and deserialization of all MQTT 5.0 wire data. Depends on: Module 1 (data_model).

## Sub-modules

| Directory        | Plan ref  | Contents |
|------------------|-----------|----------|
| `primitive/`     | 2.1       | Encode / decode of all MQTT primitive wire types |
| `properties/`    | 2.2       | Properties section encoder, decoder, and validator |
| `fixed_header/`  | 2.3       | Fixed-header encoder and decoder |
| `packet/`        | 2.4–2.12  | Variable-header + payload codecs for all 15 control packet types |
| `packet_reader/` | 2.13      | Top-level packet reader: reads a full wire packet and returns `AnyPacket` |

## Shared infrastructure (this directory)

| File              | Contents |
|-------------------|----------|
| `codec_error.h`   | `CodecError` enum and `CodecException` type |
| `read_buffer.h`   | `ReadBuffer` — read-only cursor over a byte span |
| `write_buffer.h`  | `WriteBuffer` — type alias for `std::vector<uint8_t>` |

## Design rules

- Encode functions append bytes to a `WriteBuffer&` (alias for `std::vector<uint8_t>`).
- Decode functions consume bytes from a `ReadBuffer&`; the cursor advances past decoded data.
- All errors are reported via `CodecException`.
- No dynamic allocation beyond `std::vector` / `std::string`.
- Every decode function is `[[nodiscard]]`.
