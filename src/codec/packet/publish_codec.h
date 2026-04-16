#pragma once

/**
 * @file publish_codec.h
 * @brief MQTT 5.0 PUBLISH and QoS acknowledgement packet encoder / decoder
 *        (Module 2.6, 2.7).
 *
 * Covers: PUBLISH, PUBACK, PUBREC, PUBREL, PUBCOMP.
 */

#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "data_model/packet/publish_packets.h"

namespace mqtt {

// ── PUBLISH (Section 3.3)
// ─────────────────────────────────────────────────────

/**
 * @brief Encodes a PUBLISH packet and appends the complete wire bytes to @p
 * buf.
 *
 * The Fixed Header flags (DUP | QoS | RETAIN) are derived from @p p.
 * A `packet_id` must be present in @p p when `qos > AtMostOnce`, and must be
 * absent for `qos == AtMostOnce`.
 *
 * @param buf Destination buffer.
 * @param p   PUBLISH packet to encode.
 * @throws CodecException(MalformedPacket)             if `packet_id` presence
 * contradicts QoS.
 * @throws CodecException(StringTooLong)               if any string exceeds 65
 * 535 bytes.
 * @throws CodecException(PropertyNotAllowed)          if a property is not
 * valid for PUBLISH.
 * @throws CodecException(DuplicateProperty)           if a non-repeatable
 * property appears twice.
 * @throws CodecException(VariableByteIntegerOverflow) if the packet is
 * unreasonably large.
 */
void encode_publish(WriteBuffer &buf, const PublishPacket &pkt);

/**
 * @brief Decodes the variable header and payload of a PUBLISH packet.
 *
 * @p buf must be bounded to exactly `remaining_length` bytes.
 * @p flags are the lower 4 bits of the Fixed Header first byte.
 *
 * @param buf   Source buffer, bounded to remaining_length bytes.
 * @param flags Fixed-header flags (DUP=bit3, QoS=bit2-1, RETAIN=bit0).
 * @return Decoded PublishPacket.
 * @throws CodecException(InvalidQoS)       if the QoS bits equal 3 (reserved).
 * @throws CodecException(MalformedPacket)  if DUP is set for QoS 0.
 * @throws CodecException(BufferTooShort)   on truncated input.
 * @throws CodecException(PropertyNotAllowed) from the properties codec.
 * @throws CodecException(DuplicateProperty) from the properties codec.
 */
[[nodiscard]] PublishPacket decode_publish(ReadBuffer &buf, uint8_t flags);

// ── PUBACK (Section 3.4)
// ──────────────────────────────────────────────────────

/**
 * @brief Encodes a PUBACK packet and appends the complete wire bytes to @p buf.
 *
 * Uses the short form (remaining_length == 2) when `reason_code == Success`
 * and `properties` is empty.
 *
 * @param buf Destination buffer.
 * @param p   PUBACK packet to encode.
 */
void encode_puback(WriteBuffer &buf, const PubackPacket &pkt);

/**
 * @brief Decodes the variable header of a PUBACK packet.
 *
 * @p buf must be bounded to exactly `remaining_length` bytes.
 *
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded PubackPacket.
 * @throws CodecException(BufferTooShort) on truncated input.
 */
[[nodiscard]] PubackPacket decode_puback(ReadBuffer &buf);

// ── PUBREC (Section 3.5)
// ──────────────────────────────────────────────────────

/**
 * @brief Encodes a PUBREC packet and appends the complete wire bytes to @p buf.
 * @param buf Destination buffer.
 * @param p   PUBREC packet to encode.
 */
void encode_pubrec(WriteBuffer &buf, const PubrecPacket &pkt);

/**
 * @brief Decodes the variable header of a PUBREC packet.
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded PubrecPacket.
 */
[[nodiscard]] PubrecPacket decode_pubrec(ReadBuffer &buf);

// ── PUBREL (Section 3.6)
// ──────────────────────────────────────────────────────

/**
 * @brief Encodes a PUBREL packet and appends the complete wire bytes to @p buf.
 *
 * PUBREL Fixed Header flags are always 0x02 per the MQTT specification.
 *
 * @param buf Destination buffer.
 * @param p   PUBREL packet to encode.
 */
void encode_pubrel(WriteBuffer &buf, const PubrelPacket &pkt);

/**
 * @brief Decodes the variable header of a PUBREL packet.
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded PubrelPacket.
 */
[[nodiscard]] PubrelPacket decode_pubrel(ReadBuffer &buf);

// ── PUBCOMP (Section 3.7)
// ─────────────────────────────────────────────────────

/**
 * @brief Encodes a PUBCOMP packet and appends the complete wire bytes to @p
 * buf.
 * @param buf Destination buffer.
 * @param p   PUBCOMP packet to encode.
 */
void encode_pubcomp(WriteBuffer &buf, const PubcompPacket &pkt);

/**
 * @brief Decodes the variable header of a PUBCOMP packet.
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded PubcompPacket.
 */
[[nodiscard]] PubcompPacket decode_pubcomp(ReadBuffer &buf);

} // namespace mqtt
