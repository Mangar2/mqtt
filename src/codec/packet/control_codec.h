#pragma once

/**
 * @file control_codec.h
 * @brief MQTT 5.0 PINGREQ, PINGRESP, DISCONNECT, and AUTH packet encoder /
 * decoder (Module 2.10, 2.11, 2.12).
 */

#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "data_model/packet/control_packets.h"

namespace mqtt {

// ── PINGREQ (Section 3.12)
// ────────────────────────────────────────────────────

/**
 * @brief Encodes a PINGREQ packet (Fixed Header only) and appends it to @p buf.
 * @param buf Destination buffer.
 */
void encode_pingreq(WriteBuffer &buf);

/**
 * @brief Validates that no payload bytes remain for a PINGREQ packet and
 * returns the empty struct.
 *
 * @p buf must be bounded to exactly `remaining_length` bytes (must be 0).
 *
 * @param buf Source buffer, bounded to remaining_length bytes (expected empty).
 * @return PingreqPacket{}.
 * @throws CodecException(MalformedPacket) if any bytes remain in @p buf.
 */
[[nodiscard]] PingreqPacket decode_pingreq(ReadBuffer &buf);

// ── PINGRESP (Section 3.13)
// ───────────────────────────────────────────────────

/**
 * @brief Encodes a PINGRESP packet (Fixed Header only) and appends it to @p
 * buf.
 * @param buf Destination buffer.
 */
void encode_pingresp(WriteBuffer &buf);

/**
 * @brief Validates that no payload bytes remain for a PINGRESP packet and
 * returns the empty struct.
 *
 * @param buf Source buffer, bounded to remaining_length bytes (expected empty).
 * @return PingrespPacket{}.
 * @throws CodecException(MalformedPacket) if any bytes remain in @p buf.
 */
[[nodiscard]] PingrespPacket decode_pingresp(ReadBuffer &buf);

// ── DISCONNECT (Section 3.14)
// ─────────────────────────────────────────────────

/**
 * @brief Encodes a DISCONNECT packet and appends the complete wire bytes to @p
 * buf.
 *
 * Uses the short form (remaining_length == 0) when `reason_code == Success`
 * (Normal Disconnection, 0x00) and `properties` is empty.
 *
 * @param buf Destination buffer.
 * @param p   DISCONNECT packet to encode.
 */
void encode_disconnect(WriteBuffer &buf, const DisconnectPacket &pkt);

/**
 * @brief Decodes the variable header of a DISCONNECT packet.
 *
 * @p buf must be bounded to exactly `remaining_length` bytes.
 * - Length 0: reason_code = Success, properties = empty.
 * - Length 1: reason_code present, properties = empty.
 * - Length ≥ 2: reason_code + properties.
 *
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded DisconnectPacket.
 * @throws CodecException(BufferTooShort) on truncated input.
 */
[[nodiscard]] DisconnectPacket decode_disconnect(ReadBuffer &buf);

// ── AUTH (Section 3.15)
// ───────────────────────────────────────────────────────

/**
 * @brief Encodes an AUTH packet and appends the complete wire bytes to @p buf.
 *
 * Uses the short form (remaining_length == 0) when `reason_code == Success`
 * and `properties` is empty.
 *
 * @param buf Destination buffer.
 * @param p   AUTH packet to encode.
 * @throws CodecException(MalformedPacket) if the reason code is not one of:
 *         Success (0x00), ContinueAuthentication (0x18), ReAuthenticate (0x19).
 */
void encode_auth(WriteBuffer &buf, const AuthPacket &pkt);

/**
 * @brief Decodes the variable header of an AUTH packet.
 *
 * @p buf must be bounded to exactly `remaining_length` bytes.
 * - Length 0: reason_code = Success, properties = empty.
 * - Length 1: reason_code present, properties = empty.
 * - Length ≥ 2: reason_code + properties.
 *
 * @param buf Source buffer, bounded to remaining_length bytes.
 * @return Decoded AuthPacket.
 * @throws CodecException(BufferTooShort)  on truncated input.
 * @throws CodecException(MalformedPacket) if the reason code is not valid for
 * AUTH.
 */
[[nodiscard]] AuthPacket decode_auth(ReadBuffer &buf);

} // namespace mqtt
