#include "codec/packet/subscribe_codec.h"

#include "codec/fixed_header/fixed_header.h"
#include "codec/fixed_header/fixed_header_codec.h"
#include "codec/primitive/primitive_codec.h"
#include "codec/properties/properties_codec.h"

namespace mqtt {

// ── encode_subscribe
// ──────────────────────────────────────────────────────────

void encode_subscribe(WriteBuffer &buf, const SubscribePacket &pkt) {
  if (pkt.filters.empty()) {
    throw CodecException{CodecError::MalformedPacket,
                         "SUBSCRIBE: at least one topic filter is required"};
  }

  WriteBuffer var;
  encode_two_byte_integer(var, pkt.packet_id);
  encode_properties(var, pkt.properties, PacketType::Subscribe);

  for (const auto &flt : pkt.filters) {
    encode_utf8_string(var, flt.topic_filter);

    uint8_t opts =
        static_cast<uint8_t>(static_cast<uint8_t>(flt.options.max_qos) & 0x03U);
    if (flt.options.no_local) {
      opts |= 0x04U;
    }
    if (flt.options.retain_as_published) {
      opts |= 0x08U;
    }
    opts |= static_cast<uint8_t>((flt.options.retain_handling & 0x03U) << 4U);
    encode_byte(var, opts);
  }

  encode_fixed_header(buf, FixedHeader{PacketType::Subscribe, 0x02U,
                                       static_cast<uint32_t>(var.size())});
  buf.insert(buf.end(), var.begin(), var.end());
}

// ── decode_subscribe
// ──────────────────────────────────────────────────────────

SubscribePacket decode_subscribe(ReadBuffer &buf) {
  SubscribePacket result;
  result.packet_id = decode_two_byte_integer(buf);
  result.properties = decode_properties(buf, PacketType::Subscribe);

  while (buf.remaining() > 0U) {
    SubscribeFilter entry;
    entry.topic_filter = decode_utf8_string(buf);

    uint8_t opts_byte = decode_byte(buf);
    if ((opts_byte & 0xC0U) != 0U) {
      throw CodecException{
          CodecError::MalformedPacket,
          "SUBSCRIBE: reserved bits in subscription options must be 0"};
    }

    const uint8_t qos_raw = opts_byte & 0x03U;
    if (qos_raw == 3U) {
      throw CodecException{CodecError::InvalidQoS,
                           "SUBSCRIBE: Maximum QoS value 3 is reserved"};
    }

    const uint8_t ret_hdl = static_cast<uint8_t>((opts_byte >> 4U) & 0x03U);
    if (ret_hdl > 2U) {
      throw CodecException{
          CodecError::MalformedPacket,
          "SUBSCRIBE: Retain Handling value must be 0, 1, or 2"};
    }

    entry.options.max_qos = static_cast<QoS>(qos_raw);
    entry.options.no_local = (opts_byte & 0x04U) != 0U;
    entry.options.retain_as_published = (opts_byte & 0x08U) != 0U;
    entry.options.retain_handling = ret_hdl;

    result.filters.push_back(std::move(entry));
  }

  if (result.filters.empty()) {
    throw CodecException{CodecError::MalformedPacket,
                         "SUBSCRIBE: at least one topic filter is required"};
  }

  return result;
}

// ── encode_suback
// ─────────────────────────────────────────────────────────────

void encode_suback(WriteBuffer &buf, const SubackPacket &pkt) {
  WriteBuffer var;
  encode_two_byte_integer(var, pkt.packet_id);
  encode_properties(var, pkt.properties, PacketType::Suback);
  for (const auto reason : pkt.reason_codes) {
    encode_byte(var, static_cast<uint8_t>(reason));
  }
  encode_fixed_header(buf, FixedHeader{PacketType::Suback, 0x00U,
                                       static_cast<uint32_t>(var.size())});
  buf.insert(buf.end(), var.begin(), var.end());
}

// ── decode_suback
// ─────────────────────────────────────────────────────────────

SubackPacket decode_suback(ReadBuffer &buf) {
  SubackPacket result;
  result.packet_id = decode_two_byte_integer(buf);
  result.properties = decode_properties(buf, PacketType::Suback);
  while (buf.remaining() > 0U) {
    result.reason_codes.push_back(static_cast<ReasonCode>(decode_byte(buf)));
  }
  return result;
}

// ── encode_unsubscribe
// ────────────────────────────────────────────────────────

void encode_unsubscribe(WriteBuffer &buf, const UnsubscribePacket &pkt) {
  if (pkt.topic_filters.empty()) {
    throw CodecException{CodecError::MalformedPacket,
                         "UNSUBSCRIBE: at least one topic filter is required"};
  }

  WriteBuffer var;
  encode_two_byte_integer(var, pkt.packet_id);
  encode_properties(var, pkt.properties, PacketType::Unsubscribe);
  for (const auto &filter : pkt.topic_filters) {
    encode_utf8_string(var, filter);
  }

  encode_fixed_header(buf, FixedHeader{PacketType::Unsubscribe, 0x02U,
                                       static_cast<uint32_t>(var.size())});
  buf.insert(buf.end(), var.begin(), var.end());
}

// ── decode_unsubscribe
// ────────────────────────────────────────────────────────

UnsubscribePacket decode_unsubscribe(ReadBuffer &buf) {
  UnsubscribePacket result;
  result.packet_id = decode_two_byte_integer(buf);
  result.properties = decode_properties(buf, PacketType::Unsubscribe);
  while (buf.remaining() > 0U) {
    result.topic_filters.push_back(decode_utf8_string(buf));
  }
  if (result.topic_filters.empty()) {
    throw CodecException{CodecError::MalformedPacket,
                         "UNSUBSCRIBE: at least one topic filter is required"};
  }
  return result;
}

// ── encode_unsuback
// ───────────────────────────────────────────────────────────

void encode_unsuback(WriteBuffer &buf, const UnsubackPacket &pkt) {
  WriteBuffer var;
  encode_two_byte_integer(var, pkt.packet_id);
  encode_properties(var, pkt.properties, PacketType::Unsuback);
  for (const auto reason : pkt.reason_codes) {
    encode_byte(var, static_cast<uint8_t>(reason));
  }
  encode_fixed_header(buf, FixedHeader{PacketType::Unsuback, 0x00U,
                                       static_cast<uint32_t>(var.size())});
  buf.insert(buf.end(), var.begin(), var.end());
}

// ── decode_unsuback
// ───────────────────────────────────────────────────────────

UnsubackPacket decode_unsuback(ReadBuffer &buf) {
  UnsubackPacket result;
  result.packet_id = decode_two_byte_integer(buf);
  result.properties = decode_properties(buf, PacketType::Unsuback);
  while (buf.remaining() > 0U) {
    result.reason_codes.push_back(static_cast<ReasonCode>(decode_byte(buf)));
  }
  return result;
}

} // namespace mqtt
