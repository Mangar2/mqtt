#include "codec/packet/publish_codec.h"

#include "codec/fixed_header/fixed_header.h"
#include "codec/fixed_header/fixed_header_codec.h"
#include "codec/primitive/primitive_codec.h"
#include "codec/properties/properties_codec.h"

namespace mqtt {

namespace {

//  ACK packet helper: encode with optional short form
//

void encode_ack_packet(WriteBuffer &buf, PacketType type, uint8_t fixed_flags,
                       uint16_t packet_id, ReasonCode reason_code,
                       const std::vector<Property> &properties) {
  const bool use_short_form =
      (reason_code == ReasonCode::Success) && properties.empty();

  WriteBuffer var;
  encode_two_byte_integer(var, packet_id);

  if (!use_short_form) {
    encode_byte(var, static_cast<uint8_t>(reason_code));
    encode_properties(var, properties, type);
  }

  encode_fixed_header(
      buf, FixedHeader{type, fixed_flags, static_cast<uint32_t>(var.size())});
  buf.insert(buf.end(), var.begin(), var.end());
}

//  ACK packet helper: decode with short-form support
//

struct AckFields {
  uint16_t packet_id;
  ReasonCode reason_code;
  std::vector<Property> properties;
};

[[nodiscard]] AckFields decode_ack_packet(ReadBuffer &buf, PacketType type) {
  AckFields result;
  result.packet_id = decode_two_byte_integer(buf);
  result.reason_code = ReasonCode::Success;

  if (buf.remaining() > 0U) {
    result.reason_code = static_cast<ReasonCode>(decode_byte(buf));
    if (buf.remaining() > 0U) {
      result.properties = decode_properties(buf, type);
    }
  }
  return result;
}

} // anonymous namespace

//  encode_publish
//

void encode_publish(WriteBuffer &buf, const PublishPacket &pkt) {
  // Validate packet_id <-> QoS consistency
  const bool needs_packet_id = (pkt.qos != QoS::AtMostOnce);
  if (needs_packet_id && !pkt.packet_id.has_value()) {
    throw CodecException{CodecError::MalformedPacket,
                         "PUBLISH: packet_id required for QoS > 0"};
  }
  if (!needs_packet_id && pkt.packet_id.has_value()) {
    throw CodecException{CodecError::MalformedPacket,
                         "PUBLISH: packet_id must be absent for QoS 0"};
  }

  // Build fixed-header flags
  uint8_t pub_flags = 0U;
  if (pkt.dup) {
    pub_flags |= 0x08U;
  }
  pub_flags |=
      static_cast<uint8_t>((static_cast<uint8_t>(pkt.qos) & 0x03U) << 1U);
  if (pkt.retain) {
    pub_flags |= 0x01U;
  }

  // Variable header + payload
  WriteBuffer var;
  encode_utf8_string(var, pkt.topic);
  if (needs_packet_id) {
    encode_two_byte_integer(var, *pkt.packet_id);
  }
  encode_properties(var, pkt.properties, PacketType::Publish);

  // Payload
  var.insert(var.end(), pkt.payload.data.begin(), pkt.payload.data.end());

  encode_fixed_header(buf, FixedHeader{PacketType::Publish, pub_flags,
                                       static_cast<uint32_t>(var.size())});
  buf.insert(buf.end(), var.begin(), var.end());
}

//  decode_publish
//

PublishPacket decode_publish(ReadBuffer &buf, uint8_t flags) {
  const bool dup = (flags & 0x08U) != 0U;
  const uint8_t qos_raw = static_cast<uint8_t>((flags >> 1U) & 0x03U);
  const bool retain = (flags & 0x01U) != 0U;

  if (qos_raw == 3U) {
    throw CodecException{CodecError::InvalidQoS,
                         "PUBLISH: QoS value 3 is reserved"};
  }
  if (dup && qos_raw == 0U) {
    throw CodecException{CodecError::MalformedPacket,
                         "PUBLISH: DUP flag must be 0 for QoS 0"};
  }

  const auto qos = static_cast<QoS>(qos_raw);

  PublishPacket result;
  result.dup = dup;
  result.qos = qos;
  result.retain = retain;
  result.topic = decode_utf8_string(buf);

  if (qos != QoS::AtMostOnce) {
    result.packet_id = decode_two_byte_integer(buf);
  }

  result.properties = decode_properties(buf, PacketType::Publish);

  // Everything remaining is the payload
  if (buf.remaining() > 0U) {
    auto payload_span = buf.read_bytes(buf.remaining());
    result.payload.data.assign(payload_span.begin(), payload_span.end());
  }

  return result;
}

//  PUBACK
//

void encode_puback(WriteBuffer &buf, const PubackPacket &pkt) {
  encode_ack_packet(buf, PacketType::Puback, 0x00U, pkt.packet_id,
                    pkt.reason_code, pkt.properties);
}

PubackPacket decode_puback(ReadBuffer &buf) {
  auto [pid, rc, props] = decode_ack_packet(buf, PacketType::Puback);
  PubackPacket result;
  result.packet_id = pid;
  result.reason_code = rc;
  result.properties = std::move(props);
  return result;
}

//  PUBREC
//

void encode_pubrec(WriteBuffer &buf, const PubrecPacket &pkt) {
  encode_ack_packet(buf, PacketType::Pubrec, 0x00U, pkt.packet_id,
                    pkt.reason_code, pkt.properties);
}

PubrecPacket decode_pubrec(ReadBuffer &buf) {
  auto [pid, rc, props] = decode_ack_packet(buf, PacketType::Pubrec);
  PubrecPacket result;
  result.packet_id = pid;
  result.reason_code = rc;
  result.properties = std::move(props);
  return result;
}

//  PUBREL
//

void encode_pubrel(WriteBuffer &buf, const PubrelPacket &pkt) {
  encode_ack_packet(buf, PacketType::Pubrel, 0x02U, pkt.packet_id,
                    pkt.reason_code, pkt.properties);
}

PubrelPacket decode_pubrel(ReadBuffer &buf) {
  auto [pid, rc, props] = decode_ack_packet(buf, PacketType::Pubrel);
  PubrelPacket result;
  result.packet_id = pid;
  result.reason_code = rc;
  result.properties = std::move(props);
  return result;
}

//  PUBCOMP
//

void encode_pubcomp(WriteBuffer &buf, const PubcompPacket &pkt) {
  encode_ack_packet(buf, PacketType::Pubcomp, 0x00U, pkt.packet_id,
                    pkt.reason_code, pkt.properties);
}

PubcompPacket decode_pubcomp(ReadBuffer &buf) {
  auto [pid, rc, props] = decode_ack_packet(buf, PacketType::Pubcomp);
  PubcompPacket result;
  result.packet_id = pid;
  result.reason_code = rc;
  result.properties = std::move(props);
  return result;
}

} // namespace mqtt
