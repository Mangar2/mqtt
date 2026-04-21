#include "codec/packet/connect_codec.h"

#include "codec/fixed_header/fixed_header.h"
#include "codec/fixed_header/fixed_header_codec.h"
#include "codec/primitive/primitive_codec.h"
#include "codec/properties/properties_codec.h"

namespace mqtt {

namespace {

//  CONNECT helpers
//

void encode_connect_flags(WriteBuffer &buf, const ConnectPacket &pkt) {
  uint8_t flags = 0;
  if (pkt.clean_start) {
    flags |= 0x02U;
  }
  if (pkt.will) {
    flags |= 0x04U;
    flags |= static_cast<uint8_t>((static_cast<uint8_t>(pkt.will->qos) & 0x03U)
                                  << 3U);
    if (pkt.will->retain) {
      flags |= 0x20U;
    }
  }
  if (pkt.username) {
    flags |= 0x80U;
  }
  if (pkt.password) {
    flags |= 0x40U;
  }
  encode_byte(buf, flags);
}

} // anonymous namespace

//  encode_connect
//

void encode_connect(WriteBuffer &buf, const ConnectPacket &pkt) {
  WriteBuffer var;

  // Protocol Name + Level
  encode_utf8_string(var, Utf8String{"MQTT"});
  encode_byte(var, 0x05U);

  // Connect Flags
  encode_connect_flags(var, pkt);

  // Keep Alive
  encode_two_byte_integer(var, pkt.keep_alive);

  // Connect Properties
  encode_properties(var, pkt.properties, PacketType::Connect);

  // Payload — Client ID
  encode_utf8_string(var, pkt.client_id);

  // Payload — Will (optional)
  if (pkt.will) {
    encode_properties(var, pkt.will->properties, PacketType::Will);
    encode_utf8_string(var, pkt.will->topic);
    encode_binary_data(var, pkt.will->payload);
  }

  // Payload — Username / Password (optional)
  if (pkt.username) {
    encode_utf8_string(var, *pkt.username);
  }
  if (pkt.password) {
    encode_binary_data(var, *pkt.password);
  }

  // Fixed Header
  encode_fixed_header(buf, FixedHeader{PacketType::Connect, 0x00U,
                                       static_cast<uint32_t>(var.size())});
  buf.insert(buf.end(), var.begin(), var.end());
}

//  decode_connect
//

ConnectPacket decode_connect(ReadBuffer &buf) {
  // Protocol Name
  Utf8String protocol_name = decode_utf8_string(buf);
  if (protocol_name.value != "MQTT") {
    throw CodecException{CodecError::InvalidProtocolName,
                         "CONNECT: protocol name must be \"MQTT\""};
  }

  // Protocol Level
  uint8_t level = decode_byte(buf);
  if (level != 0x05U) {
    throw CodecException{CodecError::InvalidProtocolVersion,
                         "CONNECT: protocol version must be 5", level};
  }

  // Connect Flags
  uint8_t flags = decode_byte(buf);
  if ((flags & 0x01U) != 0U) {
    throw CodecException{CodecError::MalformedPacket,
                         "CONNECT: reserved Connect Flags bit 0 must be 0"};
  }

  const bool clean_start = (flags & 0x02U) != 0U;
  const bool will_flag = (flags & 0x04U) != 0U;
  const auto will_qos_raw = static_cast<uint8_t>((flags >> 3U) & 0x03U);
  const bool will_retain = (flags & 0x20U) != 0U;
  const bool username_flag = (flags & 0x80U) != 0U;
  const bool password_flag = (flags & 0x40U) != 0U;

  if (!will_flag && (will_qos_raw != 0U || will_retain)) {
    throw CodecException{CodecError::MalformedPacket,
                         "CONNECT: Will QoS/Retain bits set without Will Flag"};
  }
  if (will_qos_raw == 3U) {
    throw CodecException{CodecError::InvalidQoS,
                         "CONNECT: Will QoS value 3 is reserved"};
  }
  const auto will_qos = static_cast<QoS>(will_qos_raw);

  // Keep Alive
  const TwoByteInteger keep_alive = decode_two_byte_integer(buf);

  // Connect Properties
  auto properties = decode_properties(buf, PacketType::Connect);

  // Payload — Client ID
  auto client_id = decode_utf8_string(buf);

  // Payload — Will (optional)
  std::optional<WillData> will;
  if (will_flag) {
    WillData will_data;
    will_data.qos = will_qos;
    will_data.retain = will_retain;
    will_data.properties = decode_properties(buf, PacketType::Will);
    will_data.topic = decode_utf8_string(buf);
    will_data.payload = decode_binary_data(buf);
    will = std::move(will_data);
  }

  // Payload — Username / Password (optional)
  std::optional<Utf8String> username;
  std::optional<BinaryData> password;

  if (username_flag) {
    username = decode_utf8_string(buf);
  }
  if (password_flag) {
    password = decode_binary_data(buf);
  }

  ConnectPacket result;
  result.keep_alive = keep_alive;
  result.clean_start = clean_start;
  result.client_id = std::move(client_id);
  result.will = std::move(will);
  result.username = std::move(username);
  result.password = std::move(password);
  result.properties = std::move(properties);
  return result;
}

//  encode_connack
//

void encode_connack(WriteBuffer &buf, const ConnackPacket &pkt) {
  WriteBuffer var;
  encode_byte(var, pkt.session_present ? 0x01U : 0x00U);
  encode_byte(var, static_cast<uint8_t>(pkt.reason_code));
  encode_properties(var, pkt.properties, PacketType::Connack);
  encode_fixed_header(buf, FixedHeader{PacketType::Connack, 0x00U,
                                       static_cast<uint32_t>(var.size())});
  buf.insert(buf.end(), var.begin(), var.end());
}

//  decode_connack
//

ConnackPacket decode_connack(ReadBuffer &buf) {
  uint8_t ack_flags = decode_byte(buf);
  if ((ack_flags & 0xFEU) != 0U) {
    throw CodecException{CodecError::MalformedPacket,
                         "CONNACK: reserved Acknowledge Flags bits must be 0"};
  }
  ConnackPacket result;
  result.session_present = (ack_flags & 0x01U) != 0U;
  result.reason_code = static_cast<ReasonCode>(decode_byte(buf));
  result.properties = decode_properties(buf, PacketType::Connack);
  return result;
}

} // namespace mqtt
