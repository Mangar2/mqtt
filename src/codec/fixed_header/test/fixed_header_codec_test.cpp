#include <catch2/catch_test_macros.hpp>

#include "codec/fixed_header/fixed_header_codec.h"
#include <vector>

using namespace mqtt;

//  Helpers
//

static ReadBuffer make_reader(const std::vector<uint8_t> &bytes) {
  return ReadBuffer{std::span<const uint8_t>{bytes.data(), bytes.size()}};
}

//  encode_fixed_header
//

TEST_CASE("fh_encode_connect", "[fixed_header]") {
  WriteBuffer buf;
  encode_fixed_header(buf, FixedHeader{PacketType::Connect, 0x00U, 10U});
  CHECK(buf == (std::vector<uint8_t>{0x10U, 0x0AU}));
}

TEST_CASE("fh_encode_publish_flags", "[fixed_header]") {
  // PUBLISH, DUP=1 QoS=1 RETAIN=0 → flags = 0x0A (0b1010)
  WriteBuffer buf;
  encode_fixed_header(buf, FixedHeader{PacketType::Publish, 0x0AU, 20U});
  CHECK(buf == (std::vector<uint8_t>{0x3AU, 0x14U}));
}

TEST_CASE("fh_encode_subscribe", "[fixed_header]") {
  WriteBuffer buf;
  encode_fixed_header(buf, FixedHeader{PacketType::Subscribe, 0x02U, 0U});
  CHECK(buf == (std::vector<uint8_t>{0x82U, 0x00U}));
}

TEST_CASE("fh_encode_large_remaining", "[fixed_header]") {
  WriteBuffer buf;
  encode_fixed_header(buf, FixedHeader{PacketType::Connack, 0x00U, 128U});
  // remaining=128 → VBI [0x80, 0x01]
  CHECK(buf == (std::vector<uint8_t>{0x20U, 0x80U, 0x01U}));
}

TEST_CASE("fh_encode_max_remaining", "[fixed_header]") {
  WriteBuffer buf;
  encode_fixed_header(buf, FixedHeader{PacketType::Publish, 0x00U, 268435455U});
  // remaining = VBI maximum: [0xFF, 0xFF, 0xFF, 0x7F]
  CHECK(buf.size() == 5U);
  CHECK(buf[1] == 0xFFU);
  CHECK(buf[2] == 0xFFU);
  CHECK(buf[3] == 0xFFU);
  CHECK(buf[4] == 0x7FU);
}

//  decode_fixed_header
//

TEST_CASE("fh_decode_connect", "[fixed_header]") {
  std::vector<uint8_t> data{0x10U, 0x0AU};
  auto reader = make_reader(data);
  auto header = decode_fixed_header(reader);
  CHECK(header.type == PacketType::Connect);
  CHECK(header.flags == 0x00U);
  CHECK(header.remaining_length == 10U);
}

TEST_CASE("fh_decode_publish_flags", "[fixed_header]") {
  std::vector<uint8_t> data{0x3AU, 0x14U};
  auto reader = make_reader(data);
  auto header = decode_fixed_header(reader);
  CHECK(header.type == PacketType::Publish);
  CHECK(header.flags == 0x0AU);
  CHECK(header.remaining_length == 20U);
}

TEST_CASE("fh_decode_subscribe", "[fixed_header]") {
  std::vector<uint8_t> data{0x82U, 0x00U};
  auto reader = make_reader(data);
  auto header = decode_fixed_header(reader);
  CHECK(header.type == PacketType::Subscribe);
  CHECK(header.flags == 0x02U);
  CHECK(header.remaining_length == 0U);
}

TEST_CASE("fh_decode_pubrel", "[fixed_header]") {
  std::vector<uint8_t> data{0x62U, 0x00U};
  auto reader = make_reader(data);
  auto header = decode_fixed_header(reader);
  CHECK(header.type == PacketType::Pubrel);
  CHECK(header.flags == 0x02U);
}

TEST_CASE("fh_decode_unsubscribe", "[fixed_header]") {
  std::vector<uint8_t> data{0xA2U, 0x00U};
  auto reader = make_reader(data);
  auto header = decode_fixed_header(reader);
  CHECK(header.type == PacketType::Unsubscribe);
  CHECK(header.flags == 0x02U);
}

TEST_CASE("fh_decode_multi_byte_remaining", "[fixed_header]") {
  std::vector<uint8_t> data{0x20U, 0x80U, 0x01U};
  auto reader = make_reader(data);
  auto header = decode_fixed_header(reader);
  CHECK(header.type == PacketType::Connack);
  CHECK(header.remaining_length == 128U);
}

TEST_CASE("fh_decode_type_0_reserved", "[fixed_header]") {
  std::vector<uint8_t> data{0x00U, 0x00U};
  auto reader = make_reader(data);
  try {
    (void)decode_fixed_header(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::InvalidPacketType);
  }
}

TEST_CASE("fh_decode_connect_bad_flags", "[fixed_header]") {
  std::vector<uint8_t> data{0x11U, 0x00U}; // CONNECT with flags=0x01
  auto reader = make_reader(data);
  try {
    (void)decode_fixed_header(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::InvalidFlags);
  }
}

TEST_CASE("fh_decode_subscribe_bad_flags", "[fixed_header]") {
  std::vector<uint8_t> data{0x81U,
                            0x00U}; // SUBSCRIBE with flags=0x01 (must be 0x02)
  auto reader = make_reader(data);
  try {
    (void)decode_fixed_header(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::InvalidFlags);
  }
}

TEST_CASE("fh_decode_pubrel_bad_flags", "[fixed_header]") {
  std::vector<uint8_t> data{0x60U,
                            0x00U}; // PUBREL with flags=0x00 (must be 0x02)
  auto reader = make_reader(data);
  try {
    (void)decode_fixed_header(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::InvalidFlags);
  }
}

TEST_CASE("fh_decode_truncated_no_remaining", "[fixed_header]") {
  std::vector<uint8_t> data{
      0x10U}; // CONNECT byte only, no VBI remaining length
  auto reader = make_reader(data);
  try {
    (void)decode_fixed_header(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::BufferTooShort);
  }
}

TEST_CASE("fh_decode_empty", "[fixed_header]") {
  std::vector<uint8_t> data;
  auto reader = make_reader(data);
  try {
    (void)decode_fixed_header(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::BufferTooShort);
  }
}

TEST_CASE("fh_decode_vbi_overflow", "[fixed_header]") {
  // CONNECT then malformed VBI with continuation in byte 4
  std::vector<uint8_t> data{0x10U, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
  auto reader = make_reader(data);
  try {
    (void)decode_fixed_header(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::VariableByteIntegerOverflow);
  }
}

//  Round-trip
//

TEST_CASE("fh_roundtrip", "[fixed_header]") {
  for (const auto &hdr : std::initializer_list<FixedHeader>{
           FixedHeader{PacketType::Connect, 0x00U, 0U},
           FixedHeader{PacketType::Publish, 0x0DU, 255U},
           FixedHeader{PacketType::Subscribe, 0x02U, 16384U},
           FixedHeader{PacketType::Disconnect, 0x00U, 2U},
           FixedHeader{PacketType::Auth, 0x00U, 268435455U},
       }) {
    WriteBuffer buf;
    encode_fixed_header(buf, hdr);
    auto reader = make_reader(buf);
    CHECK(decode_fixed_header(reader) == hdr);
  }
}
