#pragma once

/**
 * @file subscribe_codec.h
 * @brief MQTT 5.0 SUBSCRIBE, SUBACK, UNSUBSCRIBE, and UNSUBACK packet
 *        encoder / decoder (Module 2.8, 2.9).
 */

#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "data_model/packet/subscribe_packets.h"

namespace mqtt {

// ── SUBSCRIBE (Section 3.8)
// ───────────────────────────────────────────────────

/**
 * @brief Encodes a SUBSCRIBE packet and appends the complete wire bytes to @p
 * buf.
 *
 * @param buf Destination buffer.
 * @param p   SUBSCRIBE packet to encode. Must have at least one filter.
 * @throws CodecException(MalformedPacket)   if @p p.filters is empty.
 * @throws CodecException(StringTooLong)     if any topic filter exceeds 65 535
 * bytes.
 * @throws CodecException(PropertyNotAllowed) from the properties codec.
 * @throws CodecException(DuplicateProperty) from the properties codec.
 */
void encode_subscribe(WriteBuffer &buf, const SubscribePacket &pkt);

/**
 * @brief Decodes the variable header and payload of a SUBSCRIBE packet.
 *
 * @p buf must be bounded to exactly `remaining_length` bytes.
 *
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded SubscribePacket.
 * @throws CodecException(BufferTooShort)    on truncated input.
 * @throws CodecException(MalformedPacket)   if no filters are present, if the
 *         options byte reserved bits are non-zero, or if retain_handling > 2.
 * @throws CodecException(InvalidQoS)        if the max_qos bits equal 3.
 * @throws CodecException(PropertyNotAllowed) from the properties codec.
 * @throws CodecException(DuplicateProperty) from the properties codec.
 */
[[nodiscard]] SubscribePacket decode_subscribe(ReadBuffer &buf);

// ── SUBACK (Section 3.9)
// ──────────────────────────────────────────────────────

/**
 * @brief Encodes a SUBACK packet and appends the complete wire bytes to @p buf.
 * @param buf Destination buffer.
 * @param p   SUBACK packet to encode.
 */
void encode_suback(WriteBuffer &buf, const SubackPacket &pkt);

/**
 * @brief Decodes the variable header and payload of a SUBACK packet.
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded SubackPacket.
 */
[[nodiscard]] SubackPacket decode_suback(ReadBuffer &buf);

// ── UNSUBSCRIBE (Section 3.10)
// ────────────────────────────────────────────────

/**
 * @brief Encodes an UNSUBSCRIBE packet and appends the complete wire bytes to
 * @p buf.
 *
 * @param buf Destination buffer.
 * @param p   UNSUBSCRIBE packet. Must have at least one topic filter.
 * @throws CodecException(MalformedPacket)   if @p p.topic_filters is empty.
 * @throws CodecException(StringTooLong)     if any filter exceeds 65 535 bytes.
 */
void encode_unsubscribe(WriteBuffer &buf, const UnsubscribePacket &pkt);

/**
 * @brief Decodes the variable header and payload of an UNSUBSCRIBE packet.
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded UnsubscribePacket.
 * @throws CodecException(MalformedPacket) if no topic filters are present.
 */
[[nodiscard]] UnsubscribePacket decode_unsubscribe(ReadBuffer &buf);

// ── UNSUBACK (Section 3.11)
// ───────────────────────────────────────────────────

/**
 * @brief Encodes an UNSUBACK packet and appends the complete wire bytes to @p
 * buf.
 * @param buf Destination buffer.
 * @param p   UNSUBACK packet to encode.
 */
void encode_unsuback(WriteBuffer &buf, const UnsubackPacket &pkt);

/**
 * @brief Decodes the variable header and payload of an UNSUBACK packet.
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded UnsubackPacket.
 */
[[nodiscard]] UnsubackPacket decode_unsuback(ReadBuffer &buf);

} // namespace mqtt
