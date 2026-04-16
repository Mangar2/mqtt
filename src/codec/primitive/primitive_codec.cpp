#include "codec/primitive/primitive_codec.h"

namespace mqtt {

// ── Encoding ──────────────────────────────────────────────────────────────────

void encode_byte(WriteBuffer& buf, uint8_t byte)
{
    buf.push_back(byte);
}

void encode_variable_byte_integer(WriteBuffer& buf, VariableByteInteger vbi)
{
    if (vbi.value > VariableByteInteger::k_max_value) {
        throw CodecException{CodecError::VariableByteIntegerOverflow,
            "Variable Byte Integer value exceeds maximum (268435455)"};
    }
    uint32_t value = vbi.value;
    do {
        auto encoded_byte = static_cast<uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value > 0U) {
            encoded_byte |= 0x80U;
        }
        buf.push_back(encoded_byte);
    } while (value > 0U);
}

void encode_two_byte_integer(WriteBuffer& buf, TwoByteInteger value)
{
    buf.push_back(static_cast<uint8_t>(value >> 8U));
    buf.push_back(static_cast<uint8_t>(value & 0xFFU));
}

void encode_four_byte_integer(WriteBuffer& buf, FourByteInteger value)
{
    buf.push_back(static_cast<uint8_t>((value >> 24U) & 0xFFU));
    buf.push_back(static_cast<uint8_t>((value >> 16U) & 0xFFU));
    buf.push_back(static_cast<uint8_t>((value >>  8U) & 0xFFU));
    buf.push_back(static_cast<uint8_t>( value         & 0xFFU));
}

void encode_utf8_string(WriteBuffer& buf, const Utf8String& str)
{
    if (str.value.size() > Utf8String::k_max_byte_length) {
        throw CodecException{CodecError::StringTooLong,
            "UTF-8 string exceeds maximum length (65535 bytes)"};
    }
    encode_two_byte_integer(buf, static_cast<TwoByteInteger>(str.value.size()));
    buf.insert(buf.end(), str.value.begin(), str.value.end());
}

void encode_utf8_string_pair(WriteBuffer& buf, const Utf8StringPair& pair)
{
    encode_utf8_string(buf, pair.name);
    encode_utf8_string(buf, pair.value);
}

void encode_binary_data(WriteBuffer& buf, const BinaryData& data)
{
    if (data.data.size() > BinaryData::k_max_byte_length) {
        throw CodecException{CodecError::StringTooLong,
            "Binary data exceeds maximum length (65535 bytes)"};
    }
    encode_two_byte_integer(buf, static_cast<TwoByteInteger>(data.data.size()));
    buf.insert(buf.end(), data.data.begin(), data.data.end());
}

// ── Decoding ──────────────────────────────────────────────────────────────────

uint8_t decode_byte(ReadBuffer& buf)
{
    return buf.read_byte();
}

VariableByteInteger decode_variable_byte_integer(ReadBuffer& buf)
{
    uint32_t value      = 0;
    uint32_t multiplier = 1;
    uint8_t  byte_count = 0;
    uint8_t  encoded_byte{};

    do {
        encoded_byte = buf.read_byte();
        value += static_cast<uint32_t>(encoded_byte & 0x7FU) * multiplier;
        multiplier *= 128U;
        ++byte_count;
        if (byte_count == 4U && (encoded_byte & 0x80U) != 0U) {
            throw CodecException{CodecError::VariableByteIntegerOverflow,
                "Variable Byte Integer: continuation bit set in 4th byte"};
        }
    } while ((encoded_byte & 0x80U) != 0U);

    return VariableByteInteger{value};
}

TwoByteInteger decode_two_byte_integer(ReadBuffer& buf)
{
    auto bytes = buf.read_bytes(2);
    return static_cast<TwoByteInteger>(
        (static_cast<uint16_t>(bytes[0]) << 8U) | static_cast<uint16_t>(bytes[1])
    );
}

FourByteInteger decode_four_byte_integer(ReadBuffer& buf)
{
    auto bytes = buf.read_bytes(4);
    return (static_cast<uint32_t>(bytes[0]) << 24U)
         | (static_cast<uint32_t>(bytes[1]) << 16U)
         | (static_cast<uint32_t>(bytes[2]) <<  8U)
         |  static_cast<uint32_t>(bytes[3]);
}

Utf8String decode_utf8_string(ReadBuffer& buf)
{
    TwoByteInteger len = decode_two_byte_integer(buf);
    auto bytes = buf.read_bytes(len);
    return Utf8String{std::string(bytes.begin(), bytes.end())};
}

Utf8StringPair decode_utf8_string_pair(ReadBuffer& buf)
{
    Utf8String name  = decode_utf8_string(buf);
    Utf8String value = decode_utf8_string(buf);
    return Utf8StringPair{std::move(name), std::move(value)};
}

BinaryData decode_binary_data(ReadBuffer& buf)
{
    TwoByteInteger len = decode_two_byte_integer(buf);
    auto bytes = buf.read_bytes(len);
    return BinaryData{std::vector<uint8_t>(bytes.begin(), bytes.end())};
}

} // namespace mqtt
