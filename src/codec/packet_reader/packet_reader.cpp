#include "codec/packet_reader/packet_reader.h"

#include "codec/codec_error.h"
#include "codec/fixed_header/fixed_header_codec.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "data_model/packet/packet_type.h"

namespace mqtt {

AnyPacket read_packet(ReadBuffer &buf) {
  const FixedHeader hdr = decode_fixed_header(buf);

  const auto payload_span = buf.read_bytes(hdr.remaining_length);
  ReadBuffer payload_buf{payload_span};

  switch (hdr.type) {
  case PacketType::Connect:
    return decode_connect(payload_buf);
  case PacketType::Connack:
    return decode_connack(payload_buf);
  case PacketType::Publish:
    return decode_publish(payload_buf, hdr.flags);
  case PacketType::Puback:
    return decode_puback(payload_buf);
  case PacketType::Pubrec:
    return decode_pubrec(payload_buf);
  case PacketType::Pubrel:
    return decode_pubrel(payload_buf);
  case PacketType::Pubcomp:
    return decode_pubcomp(payload_buf);
  case PacketType::Subscribe:
    return decode_subscribe(payload_buf);
  case PacketType::Suback:
    return decode_suback(payload_buf);
  case PacketType::Unsubscribe:
    return decode_unsubscribe(payload_buf);
  case PacketType::Unsuback:
    return decode_unsuback(payload_buf);
  case PacketType::Pingreq:
    return decode_pingreq(payload_buf);
  case PacketType::Pingresp:
    return decode_pingresp(payload_buf);
  case PacketType::Disconnect:
    return decode_disconnect(payload_buf);
  case PacketType::Auth:
    return decode_auth(payload_buf);
  default:
    throw CodecException{CodecError::InvalidPacketType,
                         "Unknown or reserved packet type"};
  }
}

} // namespace mqtt
