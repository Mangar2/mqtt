#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "data_model/message/message.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_state.h"
#include "data_model/types/qos.h"
#include "qos/packet_id_manager.h"
#include "qos/qos1_state_machine.h"
#include "qos/qos2_state_machine.h"
#include "qos/qos_error.h"
#include "store/inflight_store.h"

using namespace mqtt;

//
// Helpers

namespace {

Message make_message(QoS qos, std::string_view topic = "test/topic") {
  return Message{
      .topic = Utf8String{std::string(topic)},
      .payload = BinaryData{.data = {0x42}},
      .qos = qos,
      .retain = false,
      .properties = {},
  };
}

PublishPacket make_publish(QoS qos, uint16_t pid) {
  PublishPacket pkt;
  pkt.qos = qos;
  pkt.topic = Utf8String{"test/topic"};
  pkt.payload = BinaryData{.data = {0x42}};
  pkt.properties = {};
  if (qos != QoS::AtMostOnce) {
    pkt.packet_id = pid;
  }
  return pkt;
}

PubackPacket make_puback(uint16_t pid) {
  PubackPacket pkt{};
  pkt.packet_id = pid;
  return pkt;
}

PubrecPacket make_pubrec(uint16_t pid) {
  PubrecPacket pkt{};
  pkt.packet_id = pid;
  return pkt;
}

PubrelPacket make_pubrel(uint16_t pid) {
  PubrelPacket pkt{};
  pkt.packet_id = pid;
  return pkt;
}

PubcompPacket make_pubcomp(uint16_t pid) {
  PubcompPacket pkt{};
  pkt.packet_id = pid;
  return pkt;
}

std::vector<InflightEntry> collect_entries(InflightStore &store,
                                           std::string_view client_id) {
  std::vector<InflightEntry> entries;
  store.for_each(client_id,
                 [&entries](const InflightEntry &entry) { entries.push_back(entry); });
  return entries;
}

std::chrono::steady_clock::time_point outbound_timestamp(
    InflightStore &store, std::string_view client_id, uint16_t packet_id) {
  std::chrono::steady_clock::time_point timestamp{};
  const bool found = store.with_entry(
      client_id, packet_id, InflightDirection::Outbound,
      [&timestamp](const InflightEntry &entry) { timestamp = entry.timestamp; });
  CHECK(found);
  return timestamp;
}

} // anonymous namespace

//
// PacketIdManager — 5.1

TEST_CASE("allocate_returns_nonzero_ids", "[packet_id_manager]") {
  PacketIdManager mgr;
  const uint16_t pid = mgr.allocate();
  CHECK(pid != 0);
  CHECK(pid <= 65535U);
}

TEST_CASE("allocate_sequential_ids", "[packet_id_manager]") {
  PacketIdManager mgr;
  CHECK(mgr.allocate() == 1);
  CHECK(mgr.allocate() == 2);
  CHECK(mgr.allocate() == 3);
}

TEST_CASE("allocate_wraps_around", "[packet_id_manager]") {
  PacketIdManager mgr;
  // Allocate first 65535 IDs.
  for (int idx = 0; idx < 65535; ++idx) {
    (void)mgr.allocate();
  }
  // Release ID 1 so wrap-around can find it.
  mgr.release(1, InflightDirection::Outbound);
  const uint16_t new_id = mgr.allocate();
  CHECK(new_id == 1);
}

TEST_CASE("allocate_exhausted_throws", "[packet_id_manager]") {
  PacketIdManager mgr;
  for (int idx = 0; idx < 65535; ++idx) {
    (void)mgr.allocate();
  }
  CHECK_THROWS_AS(mgr.allocate(), QosException);
  try {
    (void)mgr.allocate();
  } catch (const QosException &exc) {
    CHECK(exc.error() == QosError::PacketIdExhausted);
  }
}

TEST_CASE("release_allows_reuse", "[packet_id_manager]") {
  PacketIdManager mgr;
  const uint16_t pid = mgr.allocate();
  mgr.release(pid, InflightDirection::Outbound);
  CHECK_FALSE(mgr.is_in_use(pid, InflightDirection::Outbound));
}

TEST_CASE("separate_inbound_outbound_spaces", "[packet_id_manager]") {
  PacketIdManager mgr;
  const uint16_t pid = mgr.allocate(); // outbound

  // Same numeric ID can also be registered as inbound.
  const bool registered = mgr.try_register_inbound(pid);
  CHECK(registered);
  CHECK(mgr.is_in_use(pid, InflightDirection::Outbound));
  CHECK(mgr.is_in_use(pid, InflightDirection::Inbound));
}

TEST_CASE("is_in_use_reflects_allocate_and_release", "[packet_id_manager]") {
  PacketIdManager mgr;
  const uint16_t pid = mgr.allocate();
  CHECK(mgr.is_in_use(pid, InflightDirection::Outbound));
  mgr.release(pid, InflightDirection::Outbound);
  CHECK_FALSE(mgr.is_in_use(pid, InflightDirection::Outbound));
}

TEST_CASE("try_register_inbound_new_id", "[packet_id_manager]") {
  PacketIdManager mgr;
  const bool result = mgr.try_register_inbound(42);
  CHECK(result == true);
  CHECK(mgr.is_in_use(42, InflightDirection::Inbound));
}

TEST_CASE("try_register_inbound_duplicate", "[packet_id_manager]") {
  PacketIdManager mgr;
  (void)mgr.try_register_inbound(42);
  const bool result = mgr.try_register_inbound(42);
  CHECK(result == false);
}

TEST_CASE("counts_reflect_allocations", "[packet_id_manager]") {
  PacketIdManager mgr;
  CHECK(mgr.outbound_count() == 0);
  CHECK(mgr.inbound_count() == 0);

  (void)mgr.allocate();
  (void)mgr.allocate();
  CHECK(mgr.outbound_count() == 2);

  (void)mgr.try_register_inbound(10);
  CHECK(mgr.inbound_count() == 1);
}

//
// Qos1StateMachine — 5.2

TEST_CASE("on_publish_received_returns_puback", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  const PublishPacket pkt = make_publish(QoS::AtLeastOnce, 7);
  const PubackPacket ack = Qos1StateMachine::on_publish_received(pkt);
  CHECK(ack.packet_id == 7);
}

TEST_CASE("on_publish_received_invalid_qos_throws", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  SECTION("qos0") {
    const PublishPacket pkt = make_publish(QoS::AtMostOnce, 0);
    CHECK_THROWS_AS(machine.on_publish_received(pkt), QosException);
  }

  SECTION("qos2") {
    const PublishPacket pkt = make_publish(QoS::ExactlyOnce, 1);
    CHECK_THROWS_AS(machine.on_publish_received(pkt), QosException);
  }
}

TEST_CASE("on_publish_received_missing_packet_id_throws", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  PublishPacket pkt;
  pkt.qos = QoS::AtLeastOnce;
  pkt.topic = Utf8String{"t"};
  pkt.packet_id = std::nullopt;

  CHECK_THROWS_AS(Qos1StateMachine::on_publish_received(pkt), QosException);
  try {
    (void)Qos1StateMachine::on_publish_received(pkt);
  } catch (const QosException &exc) {
    CHECK(exc.error() == QosError::InvalidPacket);
  }
}

TEST_CASE("initiate_publish_allocates_id", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  const PublishPacket pkt =
      machine.initiate_publish(make_message(QoS::AtLeastOnce));
  REQUIRE(pkt.packet_id.has_value());
  CHECK(mgr.is_in_use(pkt.packet_id.value(), InflightDirection::Outbound));
}

TEST_CASE("initiate_publish_creates_inflight_entry", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  const PublishPacket pkt =
      machine.initiate_publish(make_message(QoS::AtLeastOnce));
  const auto entries = collect_entries(store, "client1");
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].state == InflightState::WaitingForPuback);
  CHECK(entries[0].direction == InflightDirection::Outbound);
  CHECK(entries[0].packet_id == pkt.packet_id.value());
}

TEST_CASE("initiate_publish_returns_correct_packet", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  const Message msg = make_message(QoS::AtLeastOnce, "a/b");
  const PublishPacket pkt = machine.initiate_publish(msg);

  CHECK(pkt.dup == false);
  CHECK(pkt.qos == QoS::AtLeastOnce);
  CHECK(pkt.topic == msg.topic);
  CHECK(pkt.payload == msg.payload);
  CHECK(pkt.retain == msg.retain);
}

TEST_CASE("initiate_publish_invalid_qos_throws", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  CHECK_THROWS_AS(machine.initiate_publish(make_message(QoS::AtMostOnce)),
                  QosException);
  CHECK_THROWS_AS(machine.initiate_publish(make_message(QoS::ExactlyOnce)),
                  QosException);
}

TEST_CASE("on_puback_received_removes_entry", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  const PublishPacket pkt =
      machine.initiate_publish(make_message(QoS::AtLeastOnce));
  const uint16_t pid = pkt.packet_id.value();

  machine.on_puback_received(make_puback(pid));

  CHECK(collect_entries(store, "client1").empty());
  CHECK_FALSE(mgr.is_in_use(pid, InflightDirection::Outbound));
}

TEST_CASE("on_puback_received_unknown_id_throws", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  CHECK_THROWS_AS(machine.on_puback_received(make_puback(99)), QosException);
  try {
    machine.on_puback_received(make_puback(99));
  } catch (const QosException &exc) {
    CHECK(exc.error() == QosError::UnexpectedPacketId);
  }
}

TEST_CASE("retransmit_sets_dup_flag", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  const Message msg = make_message(QoS::AtLeastOnce, "a/b");
  const PublishPacket pkt = machine.initiate_publish(msg);
  const uint16_t pid = pkt.packet_id.value();

  const PublishPacket retx = machine.retransmit(pid);

  CHECK(retx.dup == true);
  CHECK(retx.qos == QoS::AtLeastOnce);
  CHECK(retx.topic == msg.topic);
  CHECK(retx.payload == msg.payload);
  CHECK(retx.packet_id == pid);
}

TEST_CASE("retransmit_updates_timestamp", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  const PublishPacket pkt =
      machine.initiate_publish(make_message(QoS::AtLeastOnce));
  const uint16_t pid = pkt.packet_id.value();
  const auto old_ts = outbound_timestamp(store, "client1", pid);

  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  (void)machine.retransmit(pid);

  const auto new_ts = outbound_timestamp(store, "client1", pid);
  CHECK(new_ts > old_ts);
}

TEST_CASE("retransmit_unknown_id_throws", "[qos1]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos1StateMachine machine("client1", mgr, store);

  CHECK_THROWS_AS(machine.retransmit(42), QosException);
  try {
    (void)machine.retransmit(42);
  } catch (const QosException &exc) {
    CHECK(exc.error() == QosError::UnexpectedPacketId);
  }
}

//
// Qos2StateMachine — 5.3

TEST_CASE("inbound_publish_creates_entry_and_returns_pubrec", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  const auto result =
      machine.on_publish_received(make_publish(QoS::ExactlyOnce, 5));

  CHECK(result.pubrec.packet_id == 5);
  CHECK_FALSE(result.is_duplicate);

  const auto entries = collect_entries(store, "client1");
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].state == InflightState::WaitingForPubrel);
  CHECK(entries[0].direction == InflightDirection::Inbound);
}

TEST_CASE("inbound_publish_duplicate_detected", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  (void)machine.on_publish_received(make_publish(QoS::ExactlyOnce, 5));
  const auto dup_result =
      machine.on_publish_received(make_publish(QoS::ExactlyOnce, 5));

  CHECK(dup_result.is_duplicate);
  CHECK(dup_result.pubrec.packet_id == 5);
  // No second entry created.
  CHECK(collect_entries(store, "client1").size() == 1);
}

TEST_CASE("inbound_publish_invalid_qos_throws", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  SECTION("qos0") {
    PublishPacket pkt = make_publish(QoS::AtMostOnce, 0);
    CHECK_THROWS_AS(machine.on_publish_received(pkt), QosException);
  }
  SECTION("qos1") {
    CHECK_THROWS_AS(
        machine.on_publish_received(make_publish(QoS::AtLeastOnce, 1)),
        QosException);
  }
}

TEST_CASE("inbound_publish_missing_packet_id_throws", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  PublishPacket pkt;
  pkt.qos = QoS::ExactlyOnce;
  pkt.topic = Utf8String{"t"};
  pkt.packet_id = std::nullopt;

  CHECK_THROWS_AS(machine.on_publish_received(pkt), QosException);
}

TEST_CASE("on_pubrel_returns_pubcomp", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  (void)machine.on_publish_received(make_publish(QoS::ExactlyOnce, 5));
  const PubcompPacket comp = machine.on_pubrel_received(make_pubrel(5));

  CHECK(comp.packet_id == 5);
  CHECK(collect_entries(store, "client1").empty());
  CHECK_FALSE(mgr.is_in_use(5, InflightDirection::Inbound));
}

TEST_CASE("on_pubrel_duplicate_after_completion_returns_pubcomp", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  (void)machine.on_publish_received(make_publish(QoS::ExactlyOnce, 5));
  const PubcompPacket first = machine.on_pubrel_received(make_pubrel(5));
  const PubcompPacket second = machine.on_pubrel_received(make_pubrel(5));

  CHECK(first.packet_id == 5);
  CHECK(second.packet_id == 5);
  CHECK(collect_entries(store, "client1").empty());
}

TEST_CASE("on_pubrel_unknown_id_throws", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  CHECK_THROWS_AS(machine.on_pubrel_received(make_pubrel(99)), QosException);
  try {
    (void)machine.on_pubrel_received(make_pubrel(99));
  } catch (const QosException &exc) {
    CHECK(exc.error() == QosError::UnexpectedPacketId);
  }
}

TEST_CASE("outbound_initiate_publish_creates_entry", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  const Message msg = make_message(QoS::ExactlyOnce, "x/y");
  const PublishPacket pkt = machine.initiate_publish(msg);

  REQUIRE(pkt.packet_id.has_value());
  CHECK(pkt.dup == false);
  CHECK(pkt.qos == QoS::ExactlyOnce);
  CHECK(pkt.topic == msg.topic);

  const auto entries = collect_entries(store, "client1");
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].state == InflightState::WaitingForPubrec);
  CHECK(entries[0].direction == InflightDirection::Outbound);
}

TEST_CASE("outbound_initiate_publish_invalid_qos_throws", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  CHECK_THROWS_AS(machine.initiate_publish(make_message(QoS::AtMostOnce)),
                  QosException);
  CHECK_THROWS_AS(machine.initiate_publish(make_message(QoS::AtLeastOnce)),
                  QosException);
}

TEST_CASE("on_pubrec_advances_state_and_returns_pubrel", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  const PublishPacket pub =
      machine.initiate_publish(make_message(QoS::ExactlyOnce));
  const uint16_t pid = pub.packet_id.value();

  const PubrelPacket rel = machine.on_pubrec_received(make_pubrec(pid));
  CHECK(rel.packet_id == pid);

  const auto entries = collect_entries(store, "client1");
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].state == InflightState::WaitingForPubcomp);
}

TEST_CASE("on_pubrec_duplicate_is_idempotent", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  const PublishPacket pub =
      machine.initiate_publish(make_message(QoS::ExactlyOnce));
  const uint16_t pid = pub.packet_id.value();

  (void)machine.on_pubrec_received(make_pubrec(pid));
  // Second PUBREC (duplicate) must not throw and must return PUBREL.
  const PubrelPacket rel2 = machine.on_pubrec_received(make_pubrec(pid));
  CHECK(rel2.packet_id == pid);
}

TEST_CASE("on_pubrec_unknown_id_throws", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  CHECK_THROWS_AS(machine.on_pubrec_received(make_pubrec(99)), QosException);
  try {
    (void)machine.on_pubrec_received(make_pubrec(99));
  } catch (const QosException &exc) {
    CHECK(exc.error() == QosError::UnexpectedPacketId);
  }
}

TEST_CASE("on_pubcomp_removes_entry", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  const PublishPacket pub =
      machine.initiate_publish(make_message(QoS::ExactlyOnce));
  const uint16_t pid = pub.packet_id.value();

  (void)machine.on_pubrec_received(make_pubrec(pid));
  machine.on_pubcomp_received(make_pubcomp(pid));

  CHECK(collect_entries(store, "client1").empty());
  CHECK_FALSE(mgr.is_in_use(pid, InflightDirection::Outbound));
}

TEST_CASE("on_pubcomp_unknown_id_throws", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  CHECK_THROWS_AS(machine.on_pubcomp_received(make_pubcomp(99)), QosException);
  try {
    machine.on_pubcomp_received(make_pubcomp(99));
  } catch (const QosException &exc) {
    CHECK(exc.error() == QosError::UnexpectedPacketId);
  }
}

TEST_CASE("retransmit_before_pubrec_returns_publish_with_dup", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  const Message msg = make_message(QoS::ExactlyOnce, "a/b");
  const PublishPacket pub = machine.initiate_publish(msg);
  const uint16_t pid = pub.packet_id.value();

  const auto retx = machine.retransmit(pid);
  REQUIRE(std::holds_alternative<PublishPacket>(retx));
  const PublishPacket &rpkt = std::get<PublishPacket>(retx);
  CHECK(rpkt.dup == true);
  CHECK(rpkt.qos == QoS::ExactlyOnce);
  CHECK(rpkt.topic == msg.topic);
  CHECK(rpkt.packet_id == pid);
}

TEST_CASE("retransmit_before_pubcomp_returns_pubrel", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  const PublishPacket pub =
      machine.initiate_publish(make_message(QoS::ExactlyOnce));
  const uint16_t pid = pub.packet_id.value();
  (void)machine.on_pubrec_received(make_pubrec(pid));

  const auto retx = machine.retransmit(pid);
  REQUIRE(std::holds_alternative<PubrelPacket>(retx));
  CHECK(std::get<PubrelPacket>(retx).packet_id == pid);
}

TEST_CASE("retransmit_updates_timestamp", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  const PublishPacket pub =
      machine.initiate_publish(make_message(QoS::ExactlyOnce));
  const uint16_t pid = pub.packet_id.value();
  const auto old_ts = outbound_timestamp(store, "client1", pid);

  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  (void)machine.retransmit(pid);

  const auto new_ts = outbound_timestamp(store, "client1", pid);
  CHECK(new_ts > old_ts);
}

TEST_CASE("retransmit_unknown_id_throws", "[qos2]") {
  PacketIdManager mgr;
  InflightStore store;
  Qos2StateMachine machine("client1", mgr, store);

  CHECK_THROWS_AS(machine.retransmit(42), QosException);
  try {
    (void)machine.retransmit(42);
  } catch (const QosException &exc) {
    CHECK(exc.error() == QosError::UnexpectedPacketId);
  }
}
