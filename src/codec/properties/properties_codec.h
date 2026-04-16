#pragma once

/**
 * @file properties_codec.h
 * @brief MQTT 5.0 properties section encoder, decoder, and validator (Module 2.2).
 *
 * The properties section on the wire is: [Length : VBI] [ID : 1 byte] [Value] ...
 * A length of 0 encodes as a single `0x00` byte (no properties).
 */

#include <vector>
#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "data_model/packet/packet_type.h"
#include "data_model/property/property.h"

namespace mqtt {

/**
 * @brief Encodes a properties section and appends it to @p buf.
 *
 * Validation performed before encoding:
 *  - Each property's value type matches the type mandated by its ID.
 *  - Each property is allowed in @p context.
 *  - No non-repeatable property (any property except UserProperty) appears more than once.
 *
 * @param buf     Destination buffer.
 * @param props   Properties to encode.
 * @param context Packet type that will carry these properties.
 * @throws CodecException(PropertyTypeMismatch)  if a property value has the wrong type for its ID.
 * @throws CodecException(PropertyNotAllowed)    if a property is not permitted in @p context.
 * @throws CodecException(DuplicateProperty)     if a non-repeatable property appears more than once.
 * @throws CodecException(VariableByteIntegerOverflow) if the encoded properties section exceeds
 *         the VBI maximum (extremely unlikely in practice).
 */
void encode_properties(WriteBuffer& buf,
                       const std::vector<Property>& props,
                       PacketType context);

/**
 * @brief Decodes a properties section from @p buf.
 *
 * Reads the VBI length prefix, then decodes every property within that range.
 * Validation performed during decoding:
 *  - Each property ID is a known MQTT 5.0 property.
 *  - Each property is allowed in @p context.
 *  - No non-repeatable property appears more than once.
 *
 * @param buf     Source buffer; the cursor is advanced past the entire properties section.
 * @param context Packet type that contains these properties.
 * @return Vector of decoded Property objects (empty when the section length is 0).
 * @throws CodecException(BufferTooShort)       on truncated input.
 * @throws CodecException(InvalidPropertyId)    on an unknown property ID byte.
 * @throws CodecException(PropertyNotAllowed)   if a property is not permitted in @p context.
 * @throws CodecException(DuplicateProperty)    if a non-repeatable property appears more than once.
 */
[[nodiscard]] std::vector<Property> decode_properties(ReadBuffer& buf, PacketType context);

} // namespace mqtt
