#include "codec/packet/control_codec.h"

#include "codec/fixed_header/fixed_header.h"
#include "codec/fixed_header/fixed_header_codec.h"
#include "codec/primitive/primitive_codec.h"
#include "codec/properties/properties_codec.h"

namespace mqtt {

namespace {

// ── AUTH reason code validator
// ────────────────────────────────────────────────

void validate_auth_reason_code(ReasonCode reason) {
  if (reason != ReasonCode::Success &&
      reason != ReasonCode::ContinueAuthentication &&
      reason != ReasonCode::ReAuthenticate) {
    throw CodecException{
        CodecError::MalformedPacket,
        "AUTH: reason code must be 0x00 (Success), "
        "0x18 (Continue Authentication), or 0x19 (Re-authenticate)"};
  }
}

// ── Encode helper: optional reason_code + properties with short-form support
// ──

void encode_optional_rc_props(WriteBuffer &buf, ReasonCode reason,
                              const std::vector<Property> &props,
                              PacketType context) {
  const bool use_short_form = (reason == ReasonCode::Success) && props.empty();

  if (!use_short_form) {
    encode_byte(buf, static_cast<uint8_t>(reason));
    encode_properties(buf, props, context);
  }
}

// ── Decode helper: optional reason_code + properties with short-form support
// ──

struct OptionalRcProps {
  ReasonCode reason_code{ReasonCode::Success};
  std::vector<Property> properties;
};

[[nodiscard]] OptionalRcProps decode_optional_rc_props(ReadBuffer &buf,
                                                       PacketType context) {
  OptionalRcProps result;
  if (buf.remaining() > 0U) {
    result.reason_code = static_cast<ReasonCode>(decode_byte(buf));
    if (buf.remaining() > 0U) {
      result.properties = decode_properties(buf, context);
    }
  }
  return result;
}

} // anonymous namespace

// ── PINGREQ
// ───────────────────────────────────────────────────────────────────

void encode_pingreq(WriteBuffer &buf) {
  encode_fixed_header(buf, FixedHeader{PacketType::Pingreq, 0x00U, 0U});
}

PingreqPacket decode_pingreq(ReadBuffer &buf) {
  if (buf.remaining() != 0U) {
    throw CodecException{CodecError::MalformedPacket,
                         "PINGREQ: remaining_length must be 0"};
  }
  return PingreqPacket{};
}

// ── PINGRESP
// ──────────────────────────────────────────────────────────────────

void encode_pingresp(WriteBuffer &buf) {
  encode_fixed_header(buf, FixedHeader{PacketType::Pingresp, 0x00U, 0U});
}

PingrespPacket decode_pingresp(ReadBuffer &buf) {
  if (buf.remaining() != 0U) {
    throw CodecException{CodecError::MalformedPacket,
                         "PINGRESP: remaining_length must be 0"};
  }
  return PingrespPacket{};
}

// ── DISCONNECT
// ────────────────────────────────────────────────────────────────

void encode_disconnect(WriteBuffer &buf, const DisconnectPacket &pkt) {
  WriteBuffer var;
  encode_optional_rc_props(var, pkt.reason_code, pkt.properties,
                           PacketType::Disconnect);
  encode_fixed_header(buf, FixedHeader{PacketType::Disconnect, 0x00U,
                                       static_cast<uint32_t>(var.size())});
  buf.insert(buf.end(), var.begin(), var.end());
}

DisconnectPacket decode_disconnect(ReadBuffer &buf) {
  auto [rc, props] = decode_optional_rc_props(buf, PacketType::Disconnect);
  DisconnectPacket result;
  result.reason_code = rc;
  result.properties = std::move(props);
  return result;
}

// ── AUTH
// ──────────────────────────────────────────────────────────────────────

void encode_auth(WriteBuffer &buf, const AuthPacket &pkt) {
  validate_auth_reason_code(pkt.reason_code);
  WriteBuffer var;
  encode_optional_rc_props(var, pkt.reason_code, pkt.properties,
                           PacketType::Auth);
  encode_fixed_header(buf, FixedHeader{PacketType::Auth, 0x00U,
                                       static_cast<uint32_t>(var.size())});
  buf.insert(buf.end(), var.begin(), var.end());
}

AuthPacket decode_auth(ReadBuffer &buf) {
  auto [rc, props] = decode_optional_rc_props(buf, PacketType::Auth);
  validate_auth_reason_code(rc);
  AuthPacket result;
  result.reason_code = rc;
  result.properties = std::move(props);
  return result;
}

} // namespace mqtt
