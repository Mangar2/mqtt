#include "qos/qos2_state_machine.h"

#include <chrono>
#include <format>

#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_entry.h"
#include "data_model/session/inflight_state.h"
#include "data_model/types/qos.h"
#include "qos/qos_error.h"

namespace mqtt {

//
// Construction

Qos2StateMachine::Qos2StateMachine(std::string_view client_id,
                                   PacketIdManager &id_mgr,
                                   InflightStore &store)
    : client_id_(client_id), id_mgr_(id_mgr), store_(store) {}

//
// Inbound (5.3.1)

Qos2InboundPublishResult
Qos2StateMachine::on_publish_received(const PublishPacket &pkt) {
  if (pkt.qos != QoS::ExactlyOnce) {
    throw QosException(QosError::InvalidPacket,
                       "QoS 2 state machine received a non-QoS-2 PUBLISH");
  }
  if (!pkt.packet_id.has_value()) {
    throw QosException(QosError::InvalidPacket,
                       "QoS 2 PUBLISH is missing a Packet ID");
  }

  const uint16_t pid = pkt.packet_id.value();
  const bool is_new = id_mgr_.try_register_inbound(pid);

  if (!is_new && !pkt.dup) {
    return Qos2InboundPublishResult{
        .pubrec = PubrecPacket{.packet_id = pid,
                               .reason_code = ReasonCode::PacketIdentifierInUse,
                               .properties = {}},
        .is_duplicate = true,
    };
  }

  if (is_new) {
    recently_completed_inbound_.erase(pid);
    const Message msg{
        .topic = pkt.topic,
        .payload = pkt.payload,
        .qos = QoS::ExactlyOnce,
        .retain = pkt.retain,
        .properties = pkt.properties,
    };
    InflightEntry entry{
        .packet_id = pid,
        .message = msg,
        .qos = QoS::ExactlyOnce,
        .state = InflightState::WaitingForPubrel,
        .direction = InflightDirection::Inbound,
        .timestamp = std::chrono::steady_clock::now(),
    };
    store_.create(client_id_, std::move(entry));
  }

  return Qos2InboundPublishResult{
      .pubrec = PubrecPacket{.packet_id = pid, .properties = {}},
      .is_duplicate = !is_new,
  };
}

PubcompPacket Qos2StateMachine::on_pubrel_received(const PubrelPacket &pkt) {
  if (recently_completed_inbound_.contains(pkt.packet_id)) {
    return PubcompPacket{.packet_id = pkt.packet_id, .properties = {}};
  }

  if (!id_mgr_.is_in_use(pkt.packet_id, InflightDirection::Inbound)) {
    throw QosException(
        QosError::UnexpectedPacketId,
        std::format("PUBREL for unknown inbound packet_id={}", pkt.packet_id));
  }
  store_.remove(client_id_, pkt.packet_id, InflightDirection::Inbound);
  id_mgr_.release(pkt.packet_id, InflightDirection::Inbound);
  recently_completed_inbound_.insert(pkt.packet_id);
  return PubcompPacket{.packet_id = pkt.packet_id, .properties = {}};
}

void Qos2StateMachine::abort_inbound(uint16_t packet_id) noexcept {
  if (!id_mgr_.is_in_use(packet_id, InflightDirection::Inbound)) {
    return;
  }
  store_.remove(client_id_, packet_id, InflightDirection::Inbound);
  id_mgr_.release(packet_id, InflightDirection::Inbound);
}

//
// Outbound (5.3.2)

PublishPacket Qos2StateMachine::initiate_publish(const Message &msg) {
  if (msg.qos != QoS::ExactlyOnce) {
    throw QosException(QosError::InvalidPacket,
                       "QoS 2 state machine initiated with non-QoS-2 message");
  }

  const uint16_t pid = id_mgr_.allocate();

  InflightEntry entry{
      .packet_id = pid,
      .message = msg,
      .qos = QoS::ExactlyOnce,
      .state = InflightState::WaitingForPubrec,
      .direction = InflightDirection::Outbound,
      .timestamp = std::chrono::steady_clock::now(),
  };
  store_.create(client_id_, std::move(entry));

  return PublishPacket{
      .dup = false,
      .qos = QoS::ExactlyOnce,
      .retain = msg.retain,
      .topic = msg.topic,
      .packet_id = pid,
      .payload = msg.payload,
      .properties = msg.properties,
  };
}

PubrelPacket Qos2StateMachine::on_pubrec_received(const PubrecPacket &pkt) {
  if (!id_mgr_.is_in_use(pkt.packet_id, InflightDirection::Outbound)) {
    throw QosException(
        QosError::UnexpectedPacketId,
        std::format("PUBREC for unknown outbound packet_id={}", pkt.packet_id));
  }
  // Advance (or keep) state to WaitingForPubcomp — idempotent on duplicate
  // PUBREC.
  store_.update(client_id_, pkt.packet_id, InflightDirection::Outbound,
                InflightState::WaitingForPubcomp);
  return PubrelPacket{.packet_id = pkt.packet_id, .properties = {}};
}

void Qos2StateMachine::on_pubcomp_received(const PubcompPacket &pkt) {
  if (!id_mgr_.is_in_use(pkt.packet_id, InflightDirection::Outbound)) {
    throw QosException(QosError::UnexpectedPacketId,
                       std::format("PUBCOMP for unknown outbound packet_id={}",
                                   pkt.packet_id));
  }
  store_.remove(client_id_, pkt.packet_id, InflightDirection::Outbound);
  id_mgr_.release(pkt.packet_id, InflightDirection::Outbound);
}

//
// Retransmission (5.3.4)

std::variant<PublishPacket, PubrelPacket>
Qos2StateMachine::retransmit(uint16_t packet_id) {
  InflightEntry original_entry;
  const bool found = store_.with_entry(
      client_id_, packet_id, InflightDirection::Outbound,
      [&original_entry](const InflightEntry &entry) { original_entry = entry; });

  if (!found) {
    throw QosException(
        QosError::UnexpectedPacketId,
        std::format("retransmit: no outbound QoS 2 entry for packet_id={}",
                    packet_id));
  }

  // Refresh the transmission timestamp.
  original_entry.timestamp = std::chrono::steady_clock::now();
  store_.remove(client_id_, packet_id, InflightDirection::Outbound);
  store_.create(client_id_, std::move(original_entry));

  InflightEntry refreshed_entry;
  (void)store_.with_entry(client_id_, packet_id, InflightDirection::Outbound,
                          [&refreshed_entry](const InflightEntry &entry) {
                            refreshed_entry = entry;
                          });

  if (refreshed_entry.state == InflightState::WaitingForPubrec) {
    return PublishPacket{
        .dup = true,
        .qos = QoS::ExactlyOnce,
        .retain = refreshed_entry.message.retain,
        .topic = refreshed_entry.message.topic,
        .packet_id = packet_id,
        .payload = refreshed_entry.message.payload,
        .properties = refreshed_entry.message.properties,
    };
  }

  // WaitingForPubcomp — resend PUBREL (no DUP flag on PUBREL per MQTT 5.0
  // spec).
  return PubrelPacket{.packet_id = packet_id, .properties = {}};
}

} // namespace mqtt
