#pragma once

/**
 * @file packet_reader.h
 * @brief MQTT 5.0 top-level packet reader (Module 2.13).
 */

#include <variant>

#include "codec/read_buffer.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/packet/subscribe_packets.h"


namespace mqtt {

/**
 * @brief A type-safe union holding any single decoded MQTT 5.0 packet.
 *
 * Alternatives follow the wire-level packet type order (MQTT spec Table 2-1).
 * Use `std::visit` or `std::get<T>` to access the concrete packet value.
 */
using AnyPacket =
    std::variant<ConnectPacket, ConnackPacket, PublishPacket, PubackPacket,
                 PubrecPacket, PubrelPacket, PubcompPacket, SubscribePacket,
                 SubackPacket, UnsubscribePacket, UnsubackPacket, PingreqPacket,
                 PingrespPacket, DisconnectPacket, AuthPacket>;

/**
 * @brief Reads one complete MQTT 5.0 packet from @p buf.
 *
 * Steps performed:
 *  1. Decodes the Fixed Header to obtain the packet type, flags, and
 *     `remaining_length`.
 *  2. Slices the next `remaining_length` bytes from @p buf into a bounded
 *     sub-buffer so that each typed decoder cannot read past its packet.
 *  3. Dispatches to the appropriate typed decoder.
 *  4. Returns the decoded packet wrapped in `AnyPacket`.
 *
 * @param buf Source buffer positioned at the start of a packet's Fixed Header.
 *            The cursor is advanced past the entire packet on success.
 * @return The decoded packet.
 * @throws CodecException(BufferTooShort)    if the buffer is too short for the
 *         Fixed Header or the declared payload.
 * @throws CodecException(InvalidPacketType) if the type nibble is 0 or outside
 *         the range 1–15 (excluding the internal Will pseudo-type).
 * @throws CodecException(InvalidFlags)      if the Fixed Header flag bits
 *         violate the MQTT 5.0 specification.
 * @throws CodecException(*)                 any error raised by the
 *         type-specific decoder, propagated unchanged.
 */
[[nodiscard]] AnyPacket read_packet(ReadBuffer &buf);

} // namespace mqtt
