# packet_reader — Module 2.13: Packet Reader

Reads a complete MQTT 5.0 wire packet from a `ReadBuffer` and returns a
type-safe value holding the decoded packet.  Depends on all packet codecs
(2.4–2.12) and the fixed-header codec (2.3).

## Files

| File                  | Plan ref | Contents |
|-----------------------|----------|----------|
| `packet_reader.h`     | 2.13     | `AnyPacket` variant type alias and `read_packet()` declaration |
| `packet_reader.cpp`   | 2.13     | `read_packet()` implementation |

## Public API

### `AnyPacket`

```cpp
using AnyPacket = std::variant<
    ConnectPacket, ConnackPacket,
    PublishPacket, PubackPacket, PubrecPacket, PubrelPacket, PubcompPacket,
    SubscribePacket, SubackPacket, UnsubscribePacket, UnsubackPacket,
    PingreqPacket, PingrespPacket,
    DisconnectPacket, AuthPacket>;
```

Holds exactly one decoded packet.  Use `std::visit` or `std::get<T>` to
access the value.

### `read_packet`

```cpp
[[nodiscard]] AnyPacket read_packet(ReadBuffer& buf);
```

Reads one complete MQTT 5.0 packet from `buf`:

1. Decodes the Fixed Header (type, flags, remaining_length) via `decode_fixed_header`.
2. Slices the next `remaining_length` bytes into a bounded sub-buffer.
3. Dispatches to the appropriate typed decoder.
4. Returns the decoded packet wrapped in `AnyPacket`.

The cursor of `buf` is advanced past the entire packet (Fixed Header +
`remaining_length` bytes) on success, and past the Fixed Header only if an
exception is thrown from the type-specific decoder.

## Error behaviour

| Error | Condition |
|-------|-----------|
| `CodecException(BufferTooShort)` | Buffer too short for Fixed Header or payload |
| `CodecException(InvalidPacketType)` | Fixed-header type nibble is 0 or 16–255 |
| `CodecException(InvalidFlags)` | Fixed-header flag bits violate spec |
| Any error from the type-specific decoder | Propagated unchanged |
