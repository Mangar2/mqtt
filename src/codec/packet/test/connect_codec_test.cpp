#include <catch2/catch_test_macros.hpp>

#include "codec/codec_error.h"
#include "codec/fixed_header/fixed_header.h"
#include "codec/fixed_header/fixed_header_codec.h"
#include "codec/packet/connect_codec.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/utf8_string.h"
#include <vector>

using namespace mqtt;

// ── Helpers
// ───────────────────────────────────────────────────────────────────

static ReadBuffer make_reader(const std::vector<uint8_t> &bytes) {
  return ReadBuffer{std::span<const uint8_t>{bytes.data(), bytes.size()}};
}

// Owns the payload bytes so the ReadBuffer it creates remains valid.
struct PayloadOwner {
  std::vector<uint8_t> data;
  ReadBuffer reader() const {
    return ReadBuffer{std::span<const uint8_t>{data.data(), data.size()}};
  }
};

static PayloadOwner extract_payload(const WriteBuffer &full_packet) {
  auto rdr = make_reader(full_packet);
  auto header = decode_fixed_header(rdr);
  auto span = rdr.read_bytes(header.remaining_length);
  PayloadOwner owner;
  owner.data.assign(span.begin(), span.end());
  return owner;
}

// ── CONNECT encode
// ────────────────────────────────────────────────────────────

TEST_CASE("connect_encode_minimal_fixed_header", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"test"};
  pkt.clean_start = true;
  pkt.keep_alive = 0;

  WriteBuffer buf;
  encode_connect(buf, pkt);

  // First byte: 0x10 (Connect, flags=0)
  CHECK(buf[0] == 0x10U);
}

TEST_CASE("connect_encode_minimal_protocol_name", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"test"};

  WriteBuffer buf;
  encode_connect(buf, pkt);

  // Variable header: protocol name "MQTT" (0x00 0x04 'M' 'Q' 'T' 'T')
  CHECK(buf[2] == 0x00U);
  CHECK(buf[3] == 0x04U);
  CHECK(buf[4] == 'M');
  CHECK(buf[5] == 'Q');
  CHECK(buf[6] == 'T');
  CHECK(buf[7] == 'T');
}

TEST_CASE("connect_encode_minimal_protocol_level", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"test"};

  WriteBuffer buf;
  encode_connect(buf, pkt);

  // Protocol level
  CHECK(buf[8] == 0x05U);
}

TEST_CASE("connect_encode_keep_alive", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"c"};
  pkt.keep_alive = 60;

  WriteBuffer buf;
  encode_connect(buf, pkt);

  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  (void)reader.read_bytes(8); // proto name(6) + level(1) + flags(1)
  CHECK(reader.read_byte() == 0x00U);
  CHECK(reader.read_byte() == 0x3CU);
}

TEST_CASE("connect_encode_clean_start_false", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"c"};
  pkt.clean_start = false;

  WriteBuffer buf;
  encode_connect(buf, pkt);

  // Connect Flags is at offset 10 (0x10 + VBI + 6 proto name + 1 level = offset
  // 9)
  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  (void)reader.read_bytes(7);
  uint8_t flags = reader.read_byte();
  CHECK((flags & 0x02U) == 0x00U);
}

TEST_CASE("connect_encode_will_qos0", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"c"};
  WillData will;
  will.qos = QoS::AtMostOnce;
  will.retain = false;
  will.topic = Utf8String{"will/topic"};
  pkt.will = will;

  WriteBuffer buf;
  encode_connect(buf, pkt);

  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  (void)reader.read_bytes(7);
  uint8_t flags = reader.read_byte();
  CHECK((flags & 0x04U) != 0U);
  CHECK((flags & 0x18U) == 0U);
  CHECK((flags & 0x20U) == 0U);
}

TEST_CASE("connect_encode_will_qos2_retain", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"c"};
  WillData will;
  will.qos = QoS::ExactlyOnce;
  will.retain = true;
  will.topic = Utf8String{"t"};
  pkt.will = will;

  WriteBuffer buf;
  encode_connect(buf, pkt);

  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  (void)reader.read_bytes(7);
  uint8_t flags = reader.read_byte();
  CHECK((flags & 0x04U) != 0U);
  CHECK(((flags >> 3U) & 0x03U) == 0x02U);
  CHECK((flags & 0x20U) != 0U);
}

TEST_CASE("connect_encode_username", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"c"};
  pkt.username = Utf8String{"user"};

  WriteBuffer buf;
  encode_connect(buf, pkt);

  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  (void)reader.read_bytes(7);
  uint8_t flags = reader.read_byte();
  CHECK((flags & 0x80U) != 0U);
  CHECK((flags & 0x40U) == 0U);
}

TEST_CASE("connect_encode_password", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"c"};
  pkt.username = Utf8String{"user"};
  pkt.password = BinaryData{{0x01U, 0x02U, 0x03U}};

  WriteBuffer buf;
  encode_connect(buf, pkt);

  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  (void)reader.read_bytes(7);
  uint8_t flags = reader.read_byte();
  CHECK((flags & 0x80U) != 0U);
  CHECK((flags & 0x40U) != 0U);
}

TEST_CASE("connect_encode_roundtrip", "[connect]") {
  ConnectPacket pkt;
  pkt.keep_alive = 30;
  pkt.clean_start = true;
  pkt.client_id = Utf8String{"my-client"};
  pkt.username = Utf8String{"admin"};
  pkt.password = BinaryData{{'s', 'e', 'c'}};
  WillData will;
  will.qos = QoS::AtLeastOnce;
  will.retain = false;
  will.topic = Utf8String{"will/t"};
  will.payload = BinaryData{{'d', 'e', 'a', 'd'}};
  pkt.will = will;

  WriteBuffer buf;
  encode_connect(buf, pkt);

  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  auto decoded = decode_connect(reader);
  CHECK(decoded == pkt);
}

// ── CONNECT decode errors
// ─────────────────────────────────────────────────────

TEST_CASE("connect_decode_bad_protocol_name", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"c"};

  WriteBuffer buf;
  encode_connect(buf, pkt);

  // Corrupt protocol name ("MQTT" → "MQTX")
  buf[7] = 'X';

  auto owner1 = extract_payload(buf);
  auto reader1 = owner1.reader();
  CHECK_THROWS_AS(decode_connect(reader1), CodecException);
  auto owner2 = extract_payload(buf);
  auto reader2 = owner2.reader();
  try {
    (void)decode_connect(reader2);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::InvalidProtocolName);
  }
}

TEST_CASE("connect_decode_bad_protocol_version", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"c"};

  WriteBuffer buf;
  encode_connect(buf, pkt);

  // Offset 8 (after 0x10, VBI, 0x00,0x04,'M','Q','T','T') is the level byte
  buf[8] = 0x04U;

  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  try {
    (void)decode_connect(reader);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::InvalidProtocolVersion);
  }
}

TEST_CASE("connect_decode_reserved_bit_set", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"c"};

  WriteBuffer buf;
  encode_connect(buf, pkt);

  // Connect Flags byte is at offset 9 (after 0x10, VBI, 6-byte proto name,
  // level)
  buf[9] = 0x01U;
  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  try {
    (void)decode_connect(reader);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("connect_decode_will_qos3", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"c"};

  WriteBuffer buf;
  encode_connect(buf, pkt);

  // Set Will Flag + QoS=3 in Connect Flags
  // Will Flag = bit 2, QoS = bits 4-3
  buf[9] = (0x04U | 0x18U);
  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  try {
    (void)decode_connect(reader);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::InvalidQoS);
  }
}

TEST_CASE("connect_decode_will_flags_without_will_flag", "[connect]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"c"};

  WriteBuffer buf;
  encode_connect(buf, pkt);

  // Set Will Retain without Will Flag
  buf[9] = 0x20U;
  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  try {
    (void)decode_connect(reader);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("connect_decode_truncated", "[connect]") {
  std::vector<uint8_t> data{0x00U, 0x04U, 'M', 'Q'}; // truncated protocol name
  auto buf = make_reader(data);
  CHECK_THROWS_AS(decode_connect(buf), CodecException);
}

// ── CONNACK encode
// ────────────────────────────────────────────────────────────

TEST_CASE("connack_encode_success_no_session", "[connack]") {
  ConnackPacket pkt;
  pkt.session_present = false;
  pkt.reason_code = ReasonCode::Success;

  WriteBuffer buf;
  encode_connack(buf, pkt);

  // [0x20, remaining, 0x00, 0x00, 0x00]
  CHECK(buf[0] == 0x20U); // Connack + flags=0
  CHECK(buf[2] == 0x00U); // Ack flags
  CHECK(buf[3] == 0x00U); // Reason code = Success
  CHECK(buf[4] == 0x00U); // Empty properties (VBI = 0)
}

TEST_CASE("connack_encode_session_present", "[connack]") {
  ConnackPacket pkt;
  pkt.session_present = true;

  WriteBuffer buf;
  encode_connack(buf, pkt);

  CHECK(buf[2] == 0x01U); // Ack flags bit 0 = session_present
}

TEST_CASE("connack_encode_roundtrip", "[connack]") {
  ConnackPacket pkt;
  pkt.session_present = true;
  pkt.reason_code = ReasonCode::Success;

  WriteBuffer buf;
  encode_connack(buf, pkt);

  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  auto decoded = decode_connack(reader);
  CHECK(decoded == pkt);
}

// ── CONNACK decode errors
// ─────────────────────────────────────────────────────

TEST_CASE("connack_decode_reserved_bits", "[connack]") {
  // Build a valid connack then corrupt ack flags
  ConnackPacket pkt;
  WriteBuffer buf;
  encode_connack(buf, pkt);

  // ack_flags is at offset 2 — set reserved bit
  buf[2] = 0x02U;

  auto owner = extract_payload(buf);
  auto reader = owner.reader();
  try {
    (void)decode_connack(reader);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}
