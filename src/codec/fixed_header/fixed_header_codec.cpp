#include "codec/fixed_header/fixed_header_codec.h"

#include "codec/primitive/primitive_codec.h"

namespace mqtt {

namespace {

[[nodiscard]] PacketType to_packet_type(uint8_t nibble)
{
    if (nibble == 0U) {
        throw CodecException{CodecError::InvalidPacketType,
            "Fixed header: packet type 0 is reserved"};
    }
    // nibble in [1, 15] maps directly to the PacketType enum values 1–15
    return static_cast<PacketType>(nibble);
}

void validate_flags(PacketType type, uint8_t flags)
{
    switch (type) {
        case PacketType::Publish:
            // Flags carry DUP, QoS, RETAIN — any combination is structurally valid.
            // QoS = 3 is invalid per Section 4.3 but is checked by the PUBLISH codec.
            return;
        case PacketType::Pubrel:
        case PacketType::Subscribe:
        case PacketType::Unsubscribe:
            if (flags != 0x02U) {
                throw CodecException{CodecError::InvalidFlags,
                    "Fixed header: flags must be 0x02 for PUBREL/SUBSCRIBE/UNSUBSCRIBE"};
            }
            return;
        default:
            if (flags != 0x00U) {
                throw CodecException{CodecError::InvalidFlags,
                    "Fixed header: reserved flags must be 0x00 for this packet type"};
            }
            return;
    }
}

} // anonymous namespace

void encode_fixed_header(WriteBuffer& buf, const FixedHeader& header)
{
    auto byte1 = static_cast<uint8_t>(
        (static_cast<uint8_t>(header.type) << 4U) | (header.flags & 0x0FU)
    );
    buf.push_back(byte1);
    encode_variable_byte_integer(buf, VariableByteInteger{header.remaining_length});
}

FixedHeader decode_fixed_header(ReadBuffer& buf)
{
    uint8_t byte1      = decode_byte(buf);
    auto    type_nibble = static_cast<uint8_t>(byte1 >> 4U);
    auto    flags       = static_cast<uint8_t>(byte1 & 0x0FU);

    PacketType type = to_packet_type(type_nibble);
    validate_flags(type, flags);

    VariableByteInteger remaining = decode_variable_byte_integer(buf);
    return FixedHeader{type, flags, remaining.value};
}

} // namespace mqtt
