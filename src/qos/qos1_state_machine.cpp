#include "qos/qos1_state_machine.h"

#include <algorithm>
#include <chrono>
#include <format>

#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_entry.h"
#include "data_model/session/inflight_state.h"
#include "data_model/types/qos.h"
#include "qos/qos_error.h"

namespace mqtt {

// ──────────────────────────────────────────────────────────────────────────────
// Construction

Qos1StateMachine::Qos1StateMachine(std::string_view client_id,
                                   PacketIdManager &id_mgr,
                                   InflightStore &store)
    : client_id_(client_id), id_mgr_(id_mgr), store_(store) {}

// ──────────────────────────────────────────────────────────────────────────────
// Inbound (5.2.1)

PubackPacket Qos1StateMachine::on_publish_received(const PublishPacket &pkt) {
  if (pkt.qos != QoS::AtLeastOnce) {
    throw QosException(QosError::InvalidPacket,
                       "QoS 1 state machine received a non-QoS-1 PUBLISH");
  }
  if (!pkt.packet_id.has_value()) {
    throw QosException(QosError::InvalidPacket,
                       "QoS 1 PUBLISH is missing a Packet ID");
  }
  return PubackPacket{.packet_id = pkt.packet_id.value(), .properties = {}};
}

// ──────────────────────────────────────────────────────────────────────────────
// Outbound (5.2.2)

PublishPacket Qos1StateMachine::initiate_publish(const Message &msg) {
  if (msg.qos != QoS::AtLeastOnce) {
    throw QosException(QosError::InvalidPacket,
                       "QoS 1 state machine initiated with non-QoS-1 message");
  }

  const uint16_t pid = id_mgr_.allocate();

  const InflightEntry entry{
      .packet_id = pid,
      .message = msg,
      .qos = QoS::AtLeastOnce,
      .state = InflightState::WaitingForPuback,
      .direction = InflightDirection::Outbound,
      .timestamp = std::chrono::steady_clock::now(),
  };
  store_.create(client_id_, entry);

  return PublishPacket{
      .dup = false,
      .qos = QoS::AtLeastOnce,
      .retain = msg.retain,
      .topic = msg.topic,
      .packet_id = pid,
      .payload = msg.payload,
      .properties = msg.properties,
  };
}

void Qos1StateMachine::on_puback_received(const PubackPacket &pkt) {
  if (!id_mgr_.is_in_use(pkt.packet_id, InflightDirection::Outbound)) {
    throw QosException(
        QosError::UnexpectedPacketId,
        std::format("PUBACK for unknown packet_id={}", pkt.packet_id));
  }
  store_.remove(client_id_, pkt.packet_id, InflightDirection::Outbound);
  id_mgr_.release(pkt.packet_id, InflightDirection::Outbound);
}

// ──────────────────────────────────────────────────────────────────────────────
// Retransmission (5.2.3)

PublishPacket Qos1StateMachine::retransmit(uint16_t packet_id) {
  const auto all_entries = store_.entries_for(client_id_);
  const auto iter =
      std::ranges::find_if(all_entries, [packet_id](const InflightEntry &ent) {
        return ent.packet_id == packet_id &&
               ent.direction == InflightDirection::Outbound;
      });

  if (iter == all_entries.end()) {
    throw QosException(
        QosError::UnexpectedPacketId,
        std::format("retransmit: no outbound entry for packet_id={}",
                    packet_id));
  }

  const InflightEntry &orig = *iter;

  // Refresh the transmission timestamp.
  InflightEntry updated = orig;
  updated.timestamp = std::chrono::steady_clock::now();
  store_.remove(client_id_, packet_id, InflightDirection::Outbound);
  store_.create(client_id_, updated);

  return PublishPacket{
      .dup = true,
      .qos = QoS::AtLeastOnce,
      .retain = orig.message.retain,
      .topic = orig.message.topic,
      .packet_id = packet_id,
      .payload = orig.message.payload,
      .properties = orig.message.properties,
  };
}

} // namespace mqtt
