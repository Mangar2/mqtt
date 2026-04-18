#pragma once

/**
 * @file connect_codec.h
 * @brief MQTT 5.0 CONNECT and CONNACK packet encoder / decoder
 * (Module 2.4, 2.5).
 */

#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "data_model/packet/connect_packet.h"

namespace mqtt {

//  CONNECT (Section 3.1)
//

/**
 * @brief Encodes a CONNECT packet and appends the complete wire bytes to @p
 * buf.
 *
 * Produces: Fixed Header | Variable Header | Payload.
 *
 * @param buf Destination buffer.
 * @param p   CONNECT packet to encode.
 * @throws CodecException(StringTooLong)               if any string exceeds 65
 * 535 bytes.
 * @throws CodecException(PropertyNotAllowed)          if a property is not
 * valid for CONNECT/Will.
 * @throws CodecException(DuplicateProperty)           if a non-repeatable
 * property appears twice.
 * @throws CodecException(VariableByteIntegerOverflow) if the packet is
 * unreasonably large.
 */
void encode_connect(WriteBuffer &buf, const ConnectPacket &pkt);

/**
 * @brief Decodes the variable header and payload of a CONNECT packet.
 *
 * @p buf must be bounded to exactly `remaining_length` bytes (the bytes
 * immediately following the Fixed Header on the wire).
 *
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded ConnectPacket.
 * @throws CodecException(BufferTooShort)              on truncated input.
 * @throws CodecException(InvalidProtocolName)         if the protocol name is
 * not "MQTT".
 * @throws CodecException(InvalidProtocolVersion)      if the protocol version
 * is not 5.
 * @throws CodecException(MalformedPacket)             if reserved flags bits
 * are non-zero, Will flags are set without the Will Flag bit, or the password
 * flag is set without the username flag.
 * @throws CodecException(InvalidQoS)                  if the Will QoS bits
 * equal 3 (reserved).
 * @throws CodecException(PropertyNotAllowed)          from the properties
 * codec.
 * @throws CodecException(DuplicateProperty)           from the properties
 * codec.
 */
[[nodiscard]] ConnectPacket decode_connect(ReadBuffer &buf);

//  CONNACK (Section 3.2)
//

/**
 * @brief Encodes a CONNACK packet and appends the complete wire bytes to @p
 * buf.
 *
 * @param buf Destination buffer.
 * @param p   CONNACK packet to encode.
 * @throws CodecException(PropertyNotAllowed)          if a property is not
 * valid for CONNACK.
 * @throws CodecException(DuplicateProperty)           if a non-repeatable
 * property appears twice.
 * @throws CodecException(VariableByteIntegerOverflow) if the packet is
 * unreasonably large.
 */
void encode_connack(WriteBuffer &buf, const ConnackPacket &pkt);

/**
 * @brief Decodes the variable header of a CONNACK packet.
 *
 * @p buf must be bounded to exactly `remaining_length` bytes.
 *
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded ConnackPacket.
 * @throws CodecException(BufferTooShort)    on truncated input.
 * @throws CodecException(MalformedPacket)   if the reserved Acknowledge Flags
 * bits are non-zero.
 * @throws CodecException(PropertyNotAllowed) from the properties codec.
 * @throws CodecException(DuplicateProperty) from the properties codec.
 */
[[nodiscard]] ConnackPacket decode_connack(ReadBuffer &buf);

} // namespace mqtt
