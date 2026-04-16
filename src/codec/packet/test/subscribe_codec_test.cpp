#include <catch2/catch_test_macros.hpp>

#include "codec/codec_error.h"
#include "codec/fixed_header/fixed_header.h"
#include "codec/fixed_header/fixed_header_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/types/utf8_string.h"
#include <vector>

using namespace mqtt;

// ── Helpers
// ───────────────────────────────────────────────────────────────────

// The PacketData struct owns both the raw bytes and provides a factory
// method to create a ReadBuffer over the payload section.
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

// ── SUBSCRIBE encode
// ──────────────────────────────────────────────────────────

TEST_CASE("subscribe_encode_single_filter", "[subscribe]") {
  SubscribePacket pkt;
  pkt.packet_id = 1U;
  SubscribeFilter flt;
  flt.topic_filter = Utf8String{"a/b"};
  flt.options.max_qos = QoS::AtLeastOnce;
  pkt.filters.push_back(flt);

  WriteBuffer buf;
  encode_subscribe(buf, pkt);

  // First byte: type=8, flags=0x02 → 0x82
  CHECK(buf[0] == 0x82U);
}

TEST_CASE("subscribe_encode_all_options", "[subscribe]") {
  SubscribePacket pkt;
  pkt.packet_id = 2U;
  SubscribeFilter flt;
  flt.topic_filter = Utf8String{"t"};
  flt.options.max_qos = QoS::ExactlyOnce; // bits 1-0 = 10
  flt.options.no_local = true;            // bit 2    = 1
  flt.options.retain_as_published = true; // bit 3    = 1
  flt.options.retain_handling = 1U;       // bits 5-4 = 01
  pkt.filters.push_back(flt);

  WriteBuffer buf;
  encode_subscribe(buf, pkt);

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  (void)reader.read_bytes(2); // packet_id
  (void)reader.read_bytes(1); // properties VBI (0x00)
  (void)reader.read_bytes(3); // topic filter "t" (len=2 bytes + 1 char byte)
  uint8_t opts = reader.read_byte();
  // Expected: QoS=2(0x02) | NL(0x04) | RAP(0x08) | RH=1(0x10) = 0x1E
  CHECK(opts == 0x1EU);
}

TEST_CASE("subscribe_encode_empty_filters", "[subscribe]") {
  SubscribePacket pkt;
  pkt.packet_id = 3U;

  WriteBuffer buf;
  try {
    encode_subscribe(buf, pkt);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("subscribe_fixed_flags", "[subscribe]") {
  SubscribePacket pkt;
  pkt.packet_id = 1U;
  SubscribeFilter flt;
  flt.topic_filter = Utf8String{"x"};
  pkt.filters.push_back(flt);

  WriteBuffer buf;
  encode_subscribe(buf, pkt);

  CHECK(buf[0] == 0x82U);
}

TEST_CASE("subscribe_roundtrip", "[subscribe]") {
  SubscribePacket pkt;
  pkt.packet_id = 99U;
  SubscribeFilter flt1;
  flt1.topic_filter = Utf8String{"sensor/#"};
  flt1.options.max_qos = QoS::AtLeastOnce;
  flt1.options.no_local = true;
  flt1.options.retain_handling = 2U;
  SubscribeFilter flt2;
  flt2.topic_filter = Utf8String{"cmd/+"};
  flt2.options.max_qos = QoS::ExactlyOnce;
  pkt.filters.push_back(flt1);
  pkt.filters.push_back(flt2);

  WriteBuffer buf;
  encode_subscribe(buf, pkt);

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  auto decoded = decode_subscribe(reader);
  CHECK(decoded == pkt);
}

// ── SUBSCRIBE decode errors
// ───────────────────────────────────────────────────

TEST_CASE("subscribe_decode_qos3", "[subscribe]") {
  // Craft a SUBSCRIBE payload with options byte = 0x03 (QoS=3)
  SubscribePacket pkt;
  pkt.packet_id = 1U;
  SubscribeFilter flt;
  flt.topic_filter = Utf8String{"t"};
  pkt.filters.push_back(flt);

  WriteBuffer buf;
  encode_subscribe(buf, pkt);

  // The options byte is the last byte of the payload
  buf.back() = 0x03U; // QoS=3

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  try {
    (void)decode_subscribe(reader);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::InvalidQoS);
  }
}

TEST_CASE("subscribe_decode_reserved_bits", "[subscribe]") {
  SubscribePacket pkt;
  pkt.packet_id = 1U;
  SubscribeFilter flt;
  flt.topic_filter = Utf8String{"t"};
  pkt.filters.push_back(flt);

  WriteBuffer buf;
  encode_subscribe(buf, pkt);

  // Corrupt options byte: set bit 7
  buf.back() = 0x80U;

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  try {
    (void)decode_subscribe(reader);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("subscribe_decode_invalid_retain_handling", "[subscribe]") {
  SubscribePacket pkt;
  pkt.packet_id = 1U;
  SubscribeFilter flt;
  flt.topic_filter = Utf8String{"t"};
  pkt.filters.push_back(flt);

  WriteBuffer buf;
  encode_subscribe(buf, pkt);

  // Retain Handling = 3 → bits 5-4 = 11 = 0x30
  buf.back() = 0x30U;

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  try {
    (void)decode_subscribe(reader);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

// ── SUBACK
// ────────────────────────────────────────────────────────────────────

TEST_CASE("suback_encode_decode", "[suback]") {
  SubackPacket pkt;
  pkt.packet_id = 3U;
  pkt.reason_codes = {ReasonCode::GrantedQoS1, ReasonCode::GrantedQoS2};

  WriteBuffer buf;
  encode_suback(buf, pkt);

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  auto decoded = decode_suback(reader);
  CHECK(decoded == pkt);
}

TEST_CASE("suback_roundtrip", "[suback]") {
  SubackPacket pkt;
  pkt.packet_id = 42U;
  pkt.reason_codes = {ReasonCode::Success, ReasonCode::NotAuthorized};

  WriteBuffer buf;
  encode_suback(buf, pkt);

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  CHECK(decode_suback(reader) == pkt);
}

// ── UNSUBSCRIBE
// ───────────────────────────────────────────────────────────────

TEST_CASE("unsubscribe_encode_single", "[unsubscribe]") {
  UnsubscribePacket pkt;
  pkt.packet_id = 10U;
  pkt.topic_filters = {Utf8String{"a/b"}};

  WriteBuffer buf;
  encode_unsubscribe(buf, pkt);

  CHECK(buf[0] == 0xA2U); // type=10, flags=0x02 → (10<<4)|2 = 0xA2
}

TEST_CASE("unsubscribe_encode_empty", "[unsubscribe]") {
  UnsubscribePacket pkt;
  pkt.packet_id = 1U;

  WriteBuffer buf;
  try {
    encode_unsubscribe(buf, pkt);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("unsubscribe_fixed_flags", "[unsubscribe]") {
  UnsubscribePacket pkt;
  pkt.packet_id = 1U;
  pkt.topic_filters = {Utf8String{"x"}};

  WriteBuffer buf;
  encode_unsubscribe(buf, pkt);

  CHECK(buf[0] == 0xA2U);
}

TEST_CASE("unsubscribe_roundtrip", "[unsubscribe]") {
  UnsubscribePacket pkt;
  pkt.packet_id = 77U;
  pkt.topic_filters = {Utf8String{"a/b"}, Utf8String{"c/d/#"}};

  WriteBuffer buf;
  encode_unsubscribe(buf, pkt);

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  auto decoded = decode_unsubscribe(reader);
  CHECK(decoded == pkt);
}

// ── UNSUBACK
// ──────────────────────────────────────────────────────────────────

TEST_CASE("unsuback_roundtrip", "[unsuback]") {
  UnsubackPacket pkt;
  pkt.packet_id = 8U;
  pkt.reason_codes = {ReasonCode::Success, ReasonCode::NoSubscriptionFound};

  WriteBuffer buf;
  encode_unsuback(buf, pkt);

  auto spl = split(std::move(buf));
  auto reader = spl.payload_buf();
  auto decoded = decode_unsuback(reader);
  CHECK(decoded == pkt);
}
