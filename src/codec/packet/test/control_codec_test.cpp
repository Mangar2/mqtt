#include <catch2/catch_test_macros.hpp>

#include "codec/codec_error.h"
#include "codec/fixed_header/fixed_header.h"
#include "codec/fixed_header/fixed_header_codec.h"
#include "codec/packet/control_codec.h"
#include "data_model/packet/control_packets.h"
#include "data_model/property/property.h"
#include "data_model/types/utf8_string.h"
#include <vector>

using namespace mqtt;

// ── Helpers
// ───────────────────────────────────────────────────────────────────

static ReadBuffer make_reader(const std::vector<uint8_t> &bytes) {
  return ReadBuffer{std::span<const uint8_t>{bytes.data(), bytes.size()}};
}

struct PacketData {
  WriteBuffer buf;
  FixedHeader fh;
  std::vector<uint8_t> payload;

  ReadBuffer payload_buf() const {
    return ReadBuffer{std::span<const uint8_t>{payload.data(), payload.size()}};
  }
};

static PacketData split(WriteBuffer src) {
  PacketData result;
  result.buf = std::move(src);
  auto rdr = ReadBuffer{
      std::span<const uint8_t>{result.buf.data(), result.buf.size()}};
  result.fh = decode_fixed_header(rdr);
  auto span = rdr.read_bytes(result.fh.remaining_length);
  result.payload.assign(span.begin(), span.end());
  return result;
}

static ReadBuffer empty_reader() {
  static const std::vector<uint8_t> empty{};
  return ReadBuffer{std::span<const uint8_t>{empty.data(), empty.size()}};
}

// ── PINGREQ
// ───────────────────────────────────────────────────────────────────

TEST_CASE("pingreq_encode", "[pingreq]") {
  WriteBuffer buf;
  encode_pingreq(buf);
  CHECK(buf == (std::vector<uint8_t>{0xC0U, 0x00U}));
}

TEST_CASE("pingreq_decode_empty", "[pingreq]") {
  auto buf = empty_reader();
  auto pkt = decode_pingreq(buf);
  CHECK(pkt == PingreqPacket{});
}

TEST_CASE("pingreq_decode_non_empty", "[pingreq]") {
  std::vector<uint8_t> extra{0x01U};
  auto buf = make_reader(extra);
  try {
    (void)decode_pingreq(buf);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

// ── PINGRESP
// ──────────────────────────────────────────────────────────────────

TEST_CASE("pingresp_encode", "[pingresp]") {
  WriteBuffer buf;
  encode_pingresp(buf);
  CHECK(buf == (std::vector<uint8_t>{0xD0U, 0x00U}));
}

TEST_CASE("pingresp_decode_empty", "[pingresp]") {
  auto buf = empty_reader();
  auto pkt = decode_pingresp(buf);
  CHECK(pkt == PingrespPacket{});
}

// ── DISCONNECT
// ────────────────────────────────────────────────────────────────

TEST_CASE("disconnect_encode_short_form", "[disconnect]") {
  DisconnectPacket pkt; // default: reason_code=Success, no properties

  WriteBuffer buf;
  encode_disconnect(buf, pkt);

  // Short form: [0xE0, 0x00]
  CHECK(buf == (std::vector<uint8_t>{0xE0U, 0x00U}));
}

TEST_CASE("disconnect_encode_with_reason", "[disconnect]") {
  DisconnectPacket pkt;
  pkt.reason_code = ReasonCode::UnspecifiedError;

  WriteBuffer buf;
  encode_disconnect(buf, pkt);

  // Not short form → remaining_length > 0
  CHECK(buf[1] > 0x00U);
}

TEST_CASE("disconnect_decode_short_form", "[disconnect]") {
  auto buf = empty_reader();
  auto pkt = decode_disconnect(buf);
  CHECK(pkt.reason_code == ReasonCode::Success);
  CHECK(pkt.properties.empty());
}

TEST_CASE("disconnect_decode_reason_only", "[disconnect]") {
  std::vector<uint8_t> data{0x80U}; // UnspecifiedError
  auto buf = make_reader(data);
  auto pkt = decode_disconnect(buf);
  CHECK(pkt.reason_code == ReasonCode::UnspecifiedError);
  CHECK(pkt.properties.empty());
}

TEST_CASE("disconnect_roundtrip", "[disconnect]") {
  DisconnectPacket pkt;
  pkt.reason_code = ReasonCode::UnspecifiedError;
  pkt.properties.push_back(Property{
      PropertyId::ReasonString, PropertyValue{Utf8String{"server overload"}}});

  WriteBuffer buf;
  encode_disconnect(buf, pkt);

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  auto decoded = decode_disconnect(reader);
  CHECK(decoded == pkt);
}

// ── AUTH
// ──────────────────────────────────────────────────────────────────────

TEST_CASE("auth_encode_short_form", "[auth]") {
  AuthPacket pkt; // default: reason_code=Success, no properties

  WriteBuffer buf;
  encode_auth(buf, pkt);

  CHECK(buf == (std::vector<uint8_t>{0xF0U, 0x00U}));
}

TEST_CASE("auth_encode_continue", "[auth]") {
  AuthPacket pkt;
  pkt.reason_code = ReasonCode::ContinueAuthentication;

  WriteBuffer buf;
  encode_auth(buf, pkt);

  // Not short form
  CHECK(buf[1] > 0x00U);
  // The reason code byte should be 0x18
  auto spl = split(std::move(buf));
  CHECK(spl.payload[0] == 0x18U);
}

TEST_CASE("auth_encode_invalid_reason_code", "[auth]") {
  AuthPacket pkt;
  pkt.reason_code = ReasonCode::MalformedPacket; // 0x81 — not valid for AUTH

  WriteBuffer buf;
  try {
    encode_auth(buf, pkt);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("auth_decode_invalid_reason_code", "[auth]") {
  std::vector<uint8_t> data{0x81U}; // MalformedPacket — not valid for AUTH
  auto buf = make_reader(data);
  try {
    (void)decode_auth(buf);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("auth_roundtrip", "[auth]") {
  AuthPacket pkt;
  pkt.reason_code = ReasonCode::ContinueAuthentication;
  pkt.properties.push_back(
      Property{PropertyId::AuthenticationMethod,
               PropertyValue{Utf8String{"SCRAM-SHA-256"}}});

  WriteBuffer buf;
  encode_auth(buf, pkt);

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  auto decoded = decode_auth(reader);
  CHECK(decoded == pkt);
}
