#include <catch2/catch_test_macros.hpp>

#include "codec/codec_error.h"
#include "codec/fixed_header/fixed_header.h"
#include "codec/fixed_header/fixed_header_codec.h"
#include "codec/packet/publish_codec.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/utf8_string.h"
#include <vector>

using namespace mqtt;

// ── Helpers
// ───────────────────────────────────────────────────────────────────

static ReadBuffer make_reader(const std::vector<uint8_t> &bytes) {
  return ReadBuffer{std::span<const uint8_t>{bytes.data(), bytes.size()}};
}

struct PacketSplit {
  WriteBuffer buf;
  FixedHeader fh;
  std::vector<uint8_t> payload;
  ReadBuffer payload_buf() const {
    return ReadBuffer{std::span<const uint8_t>{payload.data(), payload.size()}};
  }
};

static PacketSplit split(WriteBuffer src) {
  PacketSplit result;
  result.buf = std::move(src);
  auto rdr = ReadBuffer{std::span<const uint8_t>{result.buf.data(), result.buf.size()}};
  result.fh = decode_fixed_header(rdr);
  auto span = rdr.read_bytes(result.fh.remaining_length);
  result.payload.assign(span.begin(), span.end());
  return result;
}

// ── PUBLISH encode
// ────────────────────────────────────────────────────────────

TEST_CASE("publish_encode_qos0", "[publish]") {
  PublishPacket pkt;
  pkt.qos = QoS::AtMostOnce;
  pkt.topic = Utf8String{"a/b"};
  pkt.payload = BinaryData{{'h', 'e', 'l', 'l', 'o'}};

  WriteBuffer buf;
  encode_publish(buf, pkt);

  CHECK(buf[0] == 0x30U); // Publish, flags=0 (no dup, qos0, no retain)
                          // No packet_id; payload follows topic + properties
}

TEST_CASE("publish_encode_qos1", "[publish]") {
  PublishPacket pkt;
  pkt.qos = QoS::AtLeastOnce;
  pkt.topic = Utf8String{"t"};
  pkt.packet_id = 1U;

  WriteBuffer buf;
  encode_publish(buf, pkt);

  CHECK(buf[0] == 0x32U); // Publish, QoS=1 → flags = 0x02
}

TEST_CASE("publish_encode_qos2", "[publish]") {
  PublishPacket pkt;
  pkt.qos = QoS::ExactlyOnce;
  pkt.topic = Utf8String{"t"};
  pkt.packet_id = 5U;

  WriteBuffer buf;
  encode_publish(buf, pkt);

  CHECK(buf[0] == 0x34U); // Publish, QoS=2 → flags = 0x04
}

TEST_CASE("publish_encode_dup_retain", "[publish]") {
  PublishPacket pkt;
  pkt.dup = true;
  pkt.retain = true;
  pkt.qos = QoS::AtLeastOnce;
  pkt.topic = Utf8String{"t"};
  pkt.packet_id = 1U;

  WriteBuffer buf;
  encode_publish(buf, pkt);

  // flags = DUP(0x08) | QoS1(0x02) | RETAIN(0x01) = 0x0B
  CHECK(buf[0] == (0x30U | 0x0BU));
}

TEST_CASE("publish_encode_missing_packet_id", "[publish]") {
  PublishPacket pkt;
  pkt.qos = QoS::AtLeastOnce;
  pkt.topic = Utf8String{"t"};
  // no packet_id

  WriteBuffer buf;
  try {
    encode_publish(buf, pkt);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("publish_encode_unexpected_packet_id", "[publish]") {
  PublishPacket pkt;
  pkt.qos = QoS::AtMostOnce;
  pkt.topic = Utf8String{"t"};
  pkt.packet_id = 1U; // must not be set for QoS 0

  WriteBuffer buf;
  try {
    encode_publish(buf, pkt);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("publish_encode_roundtrip", "[publish]") {
  PublishPacket pkt;
  pkt.qos = QoS::ExactlyOnce;
  pkt.topic = Utf8String{"sensor/temp"};
  pkt.packet_id = 42U;
  pkt.payload = BinaryData{{'2', '3', '.', '5'}};

  WriteBuffer buf;
  encode_publish(buf, pkt);

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  auto decoded = decode_publish(reader, spl.fh.flags);
  CHECK(decoded == pkt);
}

// ── PUBLISH decode errors
// ─────────────────────────────────────────────────────

TEST_CASE("publish_decode_qos3", "[publish]") {
  // flags: QoS=3 → bits 2-1 = 11
  uint8_t bad_flags = 0x06U; // bits: 0110 → QoS=3
  std::vector<uint8_t> data{
      0x00U, 0x01U, 't', // topic "t"
      0x00U              // empty properties VBI
  };
  auto buf = make_reader(data);
  try {
    (void)decode_publish(buf, bad_flags);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::InvalidQoS);
  }
}

TEST_CASE("publish_decode_dup_qos0", "[publish]") {
  uint8_t bad_flags = 0x08U; // DUP=1, QoS=0
  std::vector<uint8_t> data{0x00U, 0x01U, 't', 0x00U};
  auto buf = make_reader(data);
  try {
    (void)decode_publish(buf, bad_flags);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("publish_decode_empty_payload", "[publish]") {
  PublishPacket pkt;
  pkt.qos = QoS::AtMostOnce;
  pkt.topic = Utf8String{"t"};
  // no payload

  WriteBuffer buf;
  encode_publish(buf, pkt);

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  auto decoded = decode_publish(reader, spl.fh.flags);
  CHECK(decoded.payload.data.empty());
}

// ── PUBACK
// ────────────────────────────────────────────────────────────────────

TEST_CASE("puback_roundtrip_short", "[puback]") {
  PubackPacket pkt;
  pkt.packet_id = 7U;
  pkt.reason_code = ReasonCode::Success;
  // empty properties → short form

  WriteBuffer buf;
  encode_puback(buf, pkt);

  // Short form: remaining_length == 2
  CHECK(buf[0] == 0x40U); // Puback, flags=0
  CHECK(buf[1] == 0x02U); // remaining_length = 2
  REQUIRE(buf.size() == 4U);

  std::vector<uint8_t> payload{buf.begin() + 2, buf.end()};
  auto reader2 =
      ReadBuffer{std::span<const uint8_t>{payload.data(), payload.size()}};
  auto decoded = decode_puback(reader2);
  CHECK(decoded.packet_id == 7U);
  CHECK(decoded.reason_code == ReasonCode::Success);
  CHECK(decoded.properties.empty());
}

TEST_CASE("puback_roundtrip_full", "[puback]") {
  PubackPacket pkt;
  pkt.packet_id = 100U;
  pkt.reason_code = ReasonCode::NoMatchingSubscribers;

  WriteBuffer buf;
  encode_puback(buf, pkt);

  // Full form: remaining_length > 2
  CHECK(buf[1] > 0x02U);

  // Decode from remaining bytes
  auto rdr = make_reader(buf);
  auto header = decode_fixed_header(rdr);
  auto span = rdr.read_bytes(header.remaining_length);
  ReadBuffer reader{span};
  auto decoded = decode_puback(reader);
  CHECK(decoded == pkt);
}

TEST_CASE("puback_decode_short_form", "[puback]") {
  // Two-byte buffer: packet_id only
  std::vector<uint8_t> data{0x00U, 0x05U}; // packet_id = 5
  auto buf = make_reader(data);
  auto decoded = decode_puback(buf);
  CHECK(decoded.packet_id == 5U);
  CHECK(decoded.reason_code == ReasonCode::Success);
  CHECK(decoded.properties.empty());
}

TEST_CASE("pubrec_roundtrip", "[pubrec]") {
  PubrecPacket pkt;
  pkt.packet_id = 55U;
  pkt.reason_code = ReasonCode::Success;

  WriteBuffer buf;
  encode_pubrec(buf, pkt);

  auto rdr = make_reader(buf);
  auto header = decode_fixed_header(rdr);
  auto span = rdr.read_bytes(header.remaining_length);
  ReadBuffer reader{span};
  auto decoded = decode_pubrec(reader);
  CHECK(decoded == pkt);
}

TEST_CASE("pubrel_fixed_flags", "[pubrel]") {
  PubrelPacket pkt;
  pkt.packet_id = 1U;

  WriteBuffer buf;
  encode_pubrel(buf, pkt);

  // PUBREL: type=6, flags=0x02 → byte = (6 << 4) | 2 = 0x62
  CHECK(buf[0] == 0x62U);
}

TEST_CASE("pubrel_roundtrip", "[pubrel]") {
  PubrelPacket pkt;
  pkt.packet_id = 10U;
  pkt.reason_code = ReasonCode::Success;

  WriteBuffer buf;
  encode_pubrel(buf, pkt);

  auto rdr = make_reader(buf);
  auto header = decode_fixed_header(rdr);
  auto span = rdr.read_bytes(header.remaining_length);
  ReadBuffer reader{span};
  auto decoded = decode_pubrel(reader);
  CHECK(decoded == pkt);
}

TEST_CASE("pubcomp_roundtrip", "[pubcomp]") {
  PubcompPacket pkt;
  pkt.packet_id = 99U;
  pkt.reason_code = ReasonCode::Success;

  WriteBuffer buf;
  encode_pubcomp(buf, pkt);

  auto rdr = make_reader(buf);
  auto header = decode_fixed_header(rdr);
  auto span = rdr.read_bytes(header.remaining_length);
  ReadBuffer reader{span};
  auto decoded = decode_pubcomp(reader);
  CHECK(decoded == pkt);
}
