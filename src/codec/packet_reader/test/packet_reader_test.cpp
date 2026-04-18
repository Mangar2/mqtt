#include <catch2/catch_test_macros.hpp>

#include "codec/codec_error.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/utf8_string.h"
#include <vector>

using namespace mqtt;

//  Helpers
//

static ReadBuffer make_reader(const std::vector<uint8_t> &bytes) {
  return ReadBuffer{std::span<const uint8_t>{bytes.data(), bytes.size()}};
}

//  CONNECT / CONNACK
//

TEST_CASE("read_packet_connect", "[packet_reader]") {
  ConnectPacket pkt;
  pkt.client_id = Utf8String{"client1"};
  pkt.clean_start = true;
  pkt.keep_alive = 30;

  WriteBuffer buf;
  encode_connect(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<ConnectPacket>(any));
  CHECK(std::get<ConnectPacket>(any) == pkt);
}

TEST_CASE("read_packet_connack", "[packet_reader]") {
  ConnackPacket pkt;
  pkt.session_present = false;
  pkt.reason_code = ReasonCode::Success;

  WriteBuffer buf;
  encode_connack(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<ConnackPacket>(any));
  CHECK(std::get<ConnackPacket>(any) == pkt);
}

//  PUBLISH
//

TEST_CASE("read_packet_publish_qos0", "[packet_reader]") {
  PublishPacket pkt;
  pkt.topic = Utf8String{"sensor/temp"};
  pkt.payload = BinaryData{{0x2AU}};
  pkt.qos = QoS::AtMostOnce;

  WriteBuffer buf;
  encode_publish(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<PublishPacket>(any));
  const auto &decoded = std::get<PublishPacket>(any);
  CHECK(decoded.topic == pkt.topic);
  CHECK(decoded.payload == pkt.payload);
  CHECK(decoded.qos == QoS::AtMostOnce);
}

TEST_CASE("read_packet_publish_qos2", "[packet_reader]") {
  PublishPacket pkt;
  pkt.topic = Utf8String{"a/b"};
  pkt.qos = QoS::ExactlyOnce;
  pkt.packet_id = 42;

  WriteBuffer buf;
  encode_publish(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<PublishPacket>(any));
  const auto &decoded = std::get<PublishPacket>(any);
  CHECK(decoded.qos == QoS::ExactlyOnce);
  REQUIRE(decoded.packet_id.has_value());
  CHECK(*decoded.packet_id == 42);
}

//  QoS ACK packets
//

TEST_CASE("read_packet_puback", "[packet_reader]") {
  PubackPacket pkt;
  pkt.packet_id = 7;
  pkt.reason_code = ReasonCode::Success;

  WriteBuffer buf;
  encode_puback(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<PubackPacket>(any));
  CHECK(std::get<PubackPacket>(any).packet_id == 7);
}

TEST_CASE("read_packet_pubrec", "[packet_reader]") {
  PubrecPacket pkt;
  pkt.packet_id = 8;

  WriteBuffer buf;
  encode_pubrec(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<PubrecPacket>(any));
  CHECK(std::get<PubrecPacket>(any).packet_id == 8);
}

TEST_CASE("read_packet_pubrel", "[packet_reader]") {
  PubrelPacket pkt;
  pkt.packet_id = 9;

  WriteBuffer buf;
  encode_pubrel(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<PubrelPacket>(any));
  CHECK(std::get<PubrelPacket>(any).packet_id == 9);
}

TEST_CASE("read_packet_pubcomp", "[packet_reader]") {
  PubcompPacket pkt;
  pkt.packet_id = 10;

  WriteBuffer buf;
  encode_pubcomp(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<PubcompPacket>(any));
  CHECK(std::get<PubcompPacket>(any).packet_id == 10);
}

//  SUBSCRIBE / SUBACK / UNSUBSCRIBE / UNSUBACK
//

TEST_CASE("read_packet_subscribe", "[packet_reader]") {
  SubscribePacket pkt;
  pkt.packet_id = 1;
  pkt.filters.push_back({Utf8String{"home/#"}, SubscribeOptions{}});

  WriteBuffer buf;
  encode_subscribe(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<SubscribePacket>(any));
  CHECK(std::get<SubscribePacket>(any) == pkt);
}

TEST_CASE("read_packet_suback", "[packet_reader]") {
  SubackPacket pkt;
  pkt.packet_id = 1;
  pkt.reason_codes = {ReasonCode::GrantedQoS1};

  WriteBuffer buf;
  encode_suback(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<SubackPacket>(any));
  CHECK(std::get<SubackPacket>(any) == pkt);
}

TEST_CASE("read_packet_unsubscribe", "[packet_reader]") {
  UnsubscribePacket pkt;
  pkt.packet_id = 2;
  pkt.topic_filters.push_back(Utf8String{"home/#"});

  WriteBuffer buf;
  encode_unsubscribe(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<UnsubscribePacket>(any));
  CHECK(std::get<UnsubscribePacket>(any) == pkt);
}

TEST_CASE("read_packet_unsuback", "[packet_reader]") {
  UnsubackPacket pkt;
  pkt.packet_id = 2;
  pkt.reason_codes = {ReasonCode::Success};

  WriteBuffer buf;
  encode_unsuback(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<UnsubackPacket>(any));
  CHECK(std::get<UnsubackPacket>(any) == pkt);
}

//  PINGREQ / PINGRESP
//

TEST_CASE("read_packet_pingreq", "[packet_reader]") {
  WriteBuffer buf;
  encode_pingreq(buf);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  CHECK(std::holds_alternative<PingreqPacket>(any));
}

TEST_CASE("read_packet_pingresp", "[packet_reader]") {
  WriteBuffer buf;
  encode_pingresp(buf);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  CHECK(std::holds_alternative<PingrespPacket>(any));
}

//  DISCONNECT / AUTH
//

TEST_CASE("read_packet_disconnect", "[packet_reader]") {
  DisconnectPacket pkt;
  pkt.reason_code = ReasonCode::Success;

  WriteBuffer buf;
  encode_disconnect(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<DisconnectPacket>(any));
  CHECK(std::get<DisconnectPacket>(any).reason_code == ReasonCode::Success);
}

TEST_CASE("read_packet_auth", "[packet_reader]") {
  AuthPacket pkt;
  pkt.reason_code = ReasonCode::Success;

  WriteBuffer buf;
  encode_auth(buf, pkt);

  auto rdr = make_reader(buf);
  const auto any = read_packet(rdr);

  REQUIRE(std::holds_alternative<AuthPacket>(any));
  CHECK(std::get<AuthPacket>(any).reason_code == ReasonCode::Success);
}

//  Buffer advancement
//

TEST_CASE("read_packet_advances_cursor", "[packet_reader]") {
  WriteBuffer buf;
  encode_pingreq(buf);
  encode_pingreq(buf);

  auto rdr = make_reader(buf);

  const auto first = read_packet(rdr);
  CHECK(std::holds_alternative<PingreqPacket>(first));

  const auto second = read_packet(rdr);
  CHECK(std::holds_alternative<PingreqPacket>(second));

  CHECK(rdr.remaining() == 0);
}

//  Error cases
//

TEST_CASE("read_packet_empty_buffer", "[packet_reader]") {
  std::vector<uint8_t> empty{};
  auto rdr = make_reader(empty);

  try {
    (void)read_packet(rdr);
    FAIL("expected CodecException");
  } catch (const CodecException &exc) {
    CHECK(exc.error() == CodecError::BufferTooShort);
  }
}

TEST_CASE("read_packet_truncated_payload", "[packet_reader]") {
  // Fixed header announces remaining_length=5 but no payload follows.
  std::vector<uint8_t> bytes{0x10U, 0x05U}; // CONNECT, remaining=5
  auto rdr = make_reader(bytes);

  try {
    (void)read_packet(rdr);
    FAIL("expected CodecException");
  } catch (const CodecException &exc) {
    CHECK(exc.error() == CodecError::BufferTooShort);
  }
}

TEST_CASE("read_packet_reserved_type_zero", "[packet_reader]") {
  std::vector<uint8_t> bytes{0x00U, 0x00U};
  auto rdr = make_reader(bytes);

  try {
    (void)read_packet(rdr);
    FAIL("expected CodecException");
  } catch (const CodecException &exc) {
    CHECK(exc.error() == CodecError::InvalidPacketType);
  }
}
