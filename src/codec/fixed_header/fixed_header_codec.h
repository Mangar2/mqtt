#pragma once

/**
 * @file fixed_header_codec.h
 * @brief MQTT 5.0 Fixed Header encoder / decoder (Module 2.3).
 */

#include "codec/fixed_header/fixed_header.h"
#include "codec/read_buffer.h"
#include "codec/write_buffer.h"

namespace mqtt {

/**
 * @brief Encodes a Fixed Header and appends it to @p buf.
 *
 * Emits `[(type << 4) | flags]` followed by `remaining_length` as a
 * Variable Byte Integer (1–4 bytes).
 *
 * @param buf    Destination buffer.
 * @param header Fixed header to encode.
 * @throws CodecException(VariableByteIntegerOverflow) if `header.remaining_length`
 *         exceeds `VariableByteInteger::k_max_value` (268 435 455).
 */
void encode_fixed_header(WriteBuffer& buf, const FixedHeader& header);

/**
 * @brief Decodes a Fixed Header from @p buf, consuming 2–5 bytes.
 *
 * Validates:
 *  - Packet type nibble is not 0 (reserved).
 *  - Flag bits match the value required by the spec for the packet type
 *    (PUBREL/SUBSCRIBE/UNSUBSCRIBE must have flags = 0x02; all others except
 *    PUBLISH must have flags = 0x00; PUBLISH flags are passed through as-is).
 *
 * @param buf Source buffer.
 * @return The decoded FixedHeader.
 * @throws CodecException(BufferTooShort)              on truncated input.
 * @throws CodecException(InvalidPacketType)           if the type nibble is 0 (reserved).
 * @throws CodecException(InvalidFlags)                if reserved flag bits are non-zero.
 * @throws CodecException(VariableByteIntegerOverflow) if the remaining-length VBI is malformed.
 */
[[nodiscard]] FixedHeader decode_fixed_header(ReadBuffer& buf);

} // namespace mqtt
