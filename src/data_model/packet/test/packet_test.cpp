#include <catch2/catch_test_macros.hpp>

#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"
#include "data_model/packet/packet_type.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/qos.h"

using namespace mqtt;

// ── PacketType
// ────────────────────────────────────────────────────────────────

TEST_CASE("pkt_type_connect_value", "[packet]") {
  STATIC_CHECK(static_cast<uint8_t>(PacketType::Connect) == 1U);
}

TEST_CASE("pkt_type_auth_value", "[packet]") {
  STATIC_CHECK(static_cast<uint8_t>(PacketType::Auth) == 15U);
}

// ── ConnectPacket (1.4.1)
// ─────────────────────────────────────────────────────

TEST_CASE("connect_defaults", "[packet][connect]") {
  ConnectPacket pkt{};
  CHECK(pkt.clean_start == true);
  CHECK(pkt.keep_alive == 0);
  CHECK(!pkt.will.has_value());
  CHECK(!pkt.username.has_value());
  CHECK(!pkt.password.has_value());
  CHECK(pkt.properties.empty());
}

TEST_CASE("connect_with_will", "[packet][connect]") {
  ConnectPacket pkt{};
  pkt.will = WillData{};
  CHECK(pkt.will.has_value());
  CHECK(pkt.will->qos == QoS::AtMostOnce);
  CHECK(pkt.will->retain == false);
}

TEST_CASE("connect_equality", "[packet][connect]") {
  ConnectPacket a{};
  ConnectPacket b{};
  CHECK(a == b);
  b.keep_alive = 60;
  CHECK(a != b);
}

TEST_CASE("will_data_equality", "[packet][connect]") {
  WillData a{};
  WillData b{};
  CHECK(a == b);
  b.retain = true;
  CHECK(a != b);
}

// ── ConnackPacket (1.4.2)
// ─────────────────────────────────────────────────────

TEST_CASE("connack_defaults", "[packet][connack]") {
  ConnackPacket pkt{};
  CHECK(pkt.session_present == false);
  CHECK(pkt.reason_code == ReasonCode::Success);
  CHECK(pkt.properties.empty());
}

TEST_CASE("connack_equality", "[packet][connack]") {
  ConnackPacket a{};
  ConnackPacket b{};
  CHECK(a == b);
  b.session_present = true;
  CHECK(a != b);
}

// ── PublishPacket (1.4.3)
// ─────────────────────────────────────────────────────

TEST_CASE("publish_defaults", "[packet][publish]") {
  PublishPacket pkt{};
  CHECK(pkt.dup == false);
  CHECK(pkt.qos == QoS::AtMostOnce);
  CHECK(pkt.retain == false);
  CHECK(!pkt.packet_id.has_value());
  CHECK(pkt.payload.data.empty());
}

TEST_CASE("publish_qos1_has_packet_id", "[packet][publish]") {
  PublishPacket pkt{};
  pkt.qos = QoS::AtLeastOnce;
  pkt.packet_id = uint16_t{42};
  CHECK(pkt.packet_id.has_value());
  CHECK(*pkt.packet_id == 42U);
}

TEST_CASE("publish_equality", "[packet][publish]") {
  PublishPacket a{};
  PublishPacket b{};
  CHECK(a == b);
  b.retain = true;
  CHECK(a != b);
}

// ── ACK packets (1.4.4–1.4.7) ────────────────────────────────────────────────

TEST_CASE("puback_defaults", "[packet][puback]") {
  PubackPacket pkt{};
  CHECK(pkt.packet_id == 0U);
  CHECK(pkt.reason_code == ReasonCode::Success);
  CHECK(pkt.properties.empty());
}

TEST_CASE("pubrec_defaults", "[packet][pubrec]") {
  PubrecPacket pkt{};
  CHECK(pkt.packet_id == 0U);
  CHECK(pkt.reason_code == ReasonCode::Success);
}

TEST_CASE("pubrel_defaults", "[packet][pubrel]") {
  PubrelPacket pkt{};
  CHECK(pkt.packet_id == 0U);
  CHECK(pkt.reason_code == ReasonCode::Success);
}

TEST_CASE("pubcomp_defaults", "[packet][pubcomp]") {
  PubcompPacket pkt{};
  CHECK(pkt.packet_id == 0U);
  CHECK(pkt.reason_code == ReasonCode::Success);
}

// ── SubscribePacket (1.4.8)
// ───────────────────────────────────────────────────

TEST_CASE("subscribe_defaults", "[packet][subscribe]") {
  SubscribePacket pkt{};
  CHECK(pkt.packet_id == 0U);
  CHECK(pkt.filters.empty());
  CHECK(pkt.properties.empty());
}

TEST_CASE("subscribe_options_defaults", "[packet][subscribe]") {
  SubscriptionOptions opts{};
  CHECK(opts.max_qos == QoS::AtMostOnce);
  CHECK(opts.no_local == false);
  CHECK(opts.retain_as_published == false);
  CHECK(opts.retain_handling == 0U);
}

// ── SubackPacket (1.4.9)
// ──────────────────────────────────────────────────────

TEST_CASE("suback_defaults", "[packet][suback]") {
  SubackPacket pkt{};
  CHECK(pkt.packet_id == 0U);
  CHECK(pkt.reason_codes.empty());
}

// ── UnsubscribePacket (1.4.10)
// ────────────────────────────────────────────────

TEST_CASE("unsubscribe_defaults", "[packet][unsubscribe]") {
  UnsubscribePacket pkt{};
  CHECK(pkt.packet_id == 0U);
  CHECK(pkt.topic_filters.empty());
}

// ── UnsubackPacket (1.4.11)
// ───────────────────────────────────────────────────

TEST_CASE("unsuback_defaults", "[packet][unsuback]") {
  UnsubackPacket pkt{};
  CHECK(pkt.packet_id == 0U);
  CHECK(pkt.reason_codes.empty());
}

// ── PINGREQ / PINGRESP (1.4.12–1.4.13) ───────────────────────────────────────

TEST_CASE("pingreq_equality", "[packet][pingreq]") {
  CHECK(PingreqPacket{} == PingreqPacket{});
}

TEST_CASE("pingresp_equality", "[packet][pingresp]") {
  CHECK(PingrespPacket{} == PingrespPacket{});
}

// ── DisconnectPacket (1.4.14)
// ─────────────────────────────────────────────────

TEST_CASE("disconnect_defaults", "[packet][disconnect]") {
  DisconnectPacket pkt{};
  CHECK(pkt.reason_code == ReasonCode::Success);
  CHECK(pkt.properties.empty());
}

// ── AuthPacket (1.4.15)
// ───────────────────────────────────────────────────────

TEST_CASE("auth_defaults", "[packet][auth]") {
  AuthPacket pkt{};
  CHECK(pkt.reason_code == ReasonCode::Success);
  CHECK(pkt.properties.empty());
}
