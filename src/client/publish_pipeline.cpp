#include "client/publish_pipeline.h"

#include <chrono>
#include <string_view>
#include <utility>

#include "codec/packet/publish_codec.h"
#include "data_model/session/inflight_direction.h"
#include "qos/qos_error.h"
#include "topic/topic_error.h"
#include "topic/topic_validator.h"

namespace mqtt {

ClientPublishPipeline::PublishStartResult
ClientPublishPipeline::begin_publish(const Message &message) {
  validate_topic_or_throw(message.topic.value);

  PublishPacket publish_packet;
  publish_packet.dup = false;
  publish_packet.qos = message.qos;
  publish_packet.retain = message.retain;
  publish_packet.topic = message.topic;
  publish_packet.payload = message.payload;
  publish_packet.properties = message.properties;

  if (message.qos == QoS::AtMostOnce) {
    return PublishStartResult{.publish_packet = std::move(publish_packet),
                              .packet_id = std::nullopt,
                              .completed = true};
  }

  const uint16_t packet_id = allocate_packet_id_or_throw();
  publish_packet.packet_id = packet_id;

  InflightEntry inflight_entry;
  inflight_entry.packet_id = packet_id;
  inflight_entry.message = message;
  inflight_entry.qos = message.qos;
  inflight_entry.state = initial_state_for_qos(message.qos);
  inflight_entry.direction = InflightDirection::Outbound;
  inflight_entry.timestamp = std::chrono::steady_clock::now();
  pending_publishes_.insert_or_assign(
      packet_id, PendingPublish{.inflight_entry = std::move(inflight_entry)});

  return PublishStartResult{.publish_packet = std::move(publish_packet),
                            .packet_id = packet_id,
                            .completed = false};
}

ClientPublishPipeline::PublishAckResult
ClientPublishPipeline::on_puback(const PubackPacket &puback_packet) {
  PendingPublish &pending_publish = find_pending_or_throw(puback_packet.packet_id);
  if (pending_publish.inflight_entry.state != InflightState::WaitingForPuback) {
    throw ClientException(ClientError::ProtocolError,
                          "received PUBACK for publish not waiting for PUBACK");
  }

  pending_publishes_.erase(puback_packet.packet_id);
  release_packet_id(puback_packet.packet_id);

  return PublishAckResult{.completed = true,
                          .send_pubrel = false,
                          .pubrel_packet = std::nullopt,
                          .reason_code = puback_packet.reason_code};
}

ClientPublishPipeline::PublishAckResult
ClientPublishPipeline::on_pubrec(const PubrecPacket &pubrec_packet) {
  PendingPublish &pending_publish = find_pending_or_throw(pubrec_packet.packet_id);
  if (pending_publish.inflight_entry.state != InflightState::WaitingForPubrec) {
    throw ClientException(ClientError::ProtocolError,
                          "received PUBREC for publish not waiting for PUBREC");
  }

  if (is_error(pubrec_packet.reason_code)) {
    pending_publishes_.erase(pubrec_packet.packet_id);
    release_packet_id(pubrec_packet.packet_id);
    return PublishAckResult{.completed = true,
                            .send_pubrel = false,
                            .pubrel_packet = std::nullopt,
                            .reason_code = pubrec_packet.reason_code};
  }

  pending_publish.inflight_entry.state = InflightState::WaitingForPubcomp;
  pending_publish.inflight_entry.timestamp = std::chrono::steady_clock::now();

  PubrelPacket pubrel_packet;
  pubrel_packet.packet_id = pubrec_packet.packet_id;

  return PublishAckResult{.completed = false,
                          .send_pubrel = true,
                          .pubrel_packet = pubrel_packet,
                          .reason_code = pubrec_packet.reason_code};
}

ClientPublishPipeline::PublishAckResult
ClientPublishPipeline::on_pubcomp(const PubcompPacket &pubcomp_packet) {
  PendingPublish &pending_publish = find_pending_or_throw(pubcomp_packet.packet_id);
  if (pending_publish.inflight_entry.state != InflightState::WaitingForPubcomp) {
    throw ClientException(ClientError::ProtocolError,
                          "received PUBCOMP for publish not waiting for PUBCOMP");
  }

  pending_publishes_.erase(pubcomp_packet.packet_id);
  release_packet_id(pubcomp_packet.packet_id);
  return PublishAckResult{.completed = true,
                          .send_pubrel = false,
                          .pubrel_packet = std::nullopt,
                          .reason_code = pubcomp_packet.reason_code};
}

bool ClientPublishPipeline::has_pending(uint16_t packet_id) const noexcept {
  return pending_publishes_.contains(packet_id);
}

std::size_t ClientPublishPipeline::pending_count() const noexcept {
  return pending_publishes_.size();
}

void ClientPublishPipeline::clear() noexcept {
  for (const auto &pending_item : pending_publishes_) {
    release_packet_id(pending_item.first);
  }
  pending_publishes_.clear();
}

WriteBuffer ClientPublishPipeline::encode_publish_frame(
    const PublishPacket &publish_packet) {
  WriteBuffer frame;
  encode_publish(frame, publish_packet);
  return frame;
}

WriteBuffer
ClientPublishPipeline::encode_pubrel_frame(const PubrelPacket &pubrel_packet) {
  WriteBuffer frame;
  encode_pubrel(frame, pubrel_packet);
  return frame;
}

InflightState ClientPublishPipeline::initial_state_for_qos(QoS qos) {
  if (qos == QoS::AtLeastOnce) {
    return InflightState::WaitingForPuback;
  }
  if (qos == QoS::ExactlyOnce) {
    return InflightState::WaitingForPubrec;
  }
  throw ClientException(ClientError::InvalidPacket,
                        "invalid QoS for tracked publish pipeline state");
}

void ClientPublishPipeline::validate_topic_or_throw(std::string_view topic_name) {
  try {
    validate_topic_name(topic_name);
  } catch (const TopicException &) {
    throw ClientException(ClientError::InvalidPacket,
                          "invalid publish topic name for outbound publish");
  }
}

uint16_t ClientPublishPipeline::allocate_packet_id_or_throw() {
  try {
    return packet_id_manager_.allocate();
  } catch (const QosException &) {
    throw ClientException(ClientError::ProtocolError,
                          "failed to allocate publish packet identifier");
  }
}

void ClientPublishPipeline::release_packet_id(uint16_t packet_id) noexcept {
  packet_id_manager_.release(packet_id, InflightDirection::Outbound);
}

ClientPublishPipeline::PendingPublish &
ClientPublishPipeline::find_pending_or_throw(uint16_t packet_id) {
  const auto pending_iter = pending_publishes_.find(packet_id);
  if (pending_iter == pending_publishes_.end()) {
    throw ClientException(ClientError::ProtocolError,
                          "received publish ACK for unknown packet id");
  }
  return pending_iter->second;
}

} // namespace mqtt
