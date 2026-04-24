#include "client_session/client_session.h"

#include <chrono>
#include <stdexcept>
#include <utility>

#include "codec/packet/publish_codec.h"
#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_state.h"
#include "monitoring/structured_tracer.h"
#include "message_router/message_expiry_controller.h"

namespace mqtt {

ClientSession::ClientSession(
    std::string client_id, std::string username,
    std::shared_ptr<IAuthenticator> authenticator,
    std::shared_ptr<OutboundQueue> outbound_queue,
    InflightStore &inflight_store, uint16_t keep_alive_seconds,
    uint16_t receive_maximum, uint16_t topic_alias_maximum,
    std::chrono::steady_clock::duration retransmit_timeout,
  uint32_t maximum_packet_size, std::string negotiated_auth_method)
    : client_id_(std::move(client_id)), username_(std::move(username)),
      outbound_queue_(std::move(outbound_queue)), packet_id_manager_(),
      qos1_state_machine_(client_id_, packet_id_manager_, inflight_store),
      qos2_state_machine_(client_id_, packet_id_manager_, inflight_store),
      receive_maximum_(receive_maximum),
      topic_alias_table_(topic_alias_maximum),
      keep_alive_timer_(keep_alive_seconds),
      enhanced_auth_handler_(std::move(authenticator)),
      inflight_store_(inflight_store), retransmit_timeout_(retransmit_timeout),
      maximum_packet_size_(maximum_packet_size) {
  if (!outbound_queue_) {
    throw std::invalid_argument(
        "ClientSession: outbound_queue must not be null");
  }
  connection_state_machine_.on_connect();
  enhanced_auth_handler_.bootstrap_connected_session(
      std::move(negotiated_auth_method));
}

AuthResult ClientSession::initiate_auth(const ConnectPacket &connect_packet) {
  return enhanced_auth_handler_.initiate(connect_packet);
}

void ClientSession::set_tracer(StructuredTracer *tracer) noexcept {
  structured_tracer_ = tracer;
}

InboundPublishResult
ClientSession::on_publish(const PublishPacket &publish_packet) {
  InboundPublishResult result;

  switch (publish_packet.qos) {
  case QoS::AtMostOnce:
    result.routable_message = message_from_publish(publish_packet);
    return result;

  case QoS::AtLeastOnce: {
    const PubackPacket puback_packet =
        Qos1StateMachine::on_publish_received(publish_packet);
    result.routable_message = message_from_publish(publish_packet);
    result.response_frames.push_back(encode_puback_packet(puback_packet));
    return result;
  }

  case QoS::ExactlyOnce: {
    const Qos2InboundPublishResult qos2_result =
        qos2_state_machine_.on_publish_received(publish_packet);
    if (!qos2_result.is_duplicate) {
      result.routable_message = message_from_publish(publish_packet);
    }
    result.response_frames.push_back(encode_pubrec_packet(qos2_result.pubrec));
    return result;
  }
  }

  throw std::invalid_argument("ClientSession::on_publish: unsupported QoS");
}

void ClientSession::on_puback(const PubackPacket &puback_packet) {
  qos1_state_machine_.on_puback_received(puback_packet);
  receive_maximum_.release();
}

WriteBuffer ClientSession::on_pubrec(const PubrecPacket &pubrec_packet) {
  const PubrelPacket pubrel_packet =
      qos2_state_machine_.on_pubrec_received(pubrec_packet);
  return encode_pubrel_packet(pubrel_packet);
}

WriteBuffer ClientSession::on_pubrel(const PubrelPacket &pubrel_packet) {
  const PubcompPacket pubcomp_packet =
      qos2_state_machine_.on_pubrel_received(pubrel_packet);
  return encode_pubcomp_packet(pubcomp_packet);
}

void ClientSession::abort_inbound_qos2(uint16_t packet_id) noexcept {
  qos2_state_machine_.abort_inbound(packet_id);
}

void ClientSession::on_pubcomp(const PubcompPacket &pubcomp_packet) {
  qos2_state_machine_.on_pubcomp_received(pubcomp_packet);
  receive_maximum_.release();
}

AuthResult ClientSession::on_auth(const AuthPacket &auth_packet) {
  if (auth_packet.reason_code == ReasonCode::ReAuthenticate) {
    return enhanced_auth_handler_.reauthenticate(auth_packet);
  }
  return enhanced_auth_handler_.on_auth(auth_packet);
}

void ClientSession::mark_session_resumed() noexcept {
  const std::vector<InflightEntry> entries = inflight_store_.entries_for(client_id_);
  for (const InflightEntry &entry : entries) {
    (void)packet_id_manager_.register_existing(entry.packet_id, entry.direction);
  }

  replay_pending_inflight_ = true;
}

std::vector<WriteBuffer> ClientSession::drain_outbound() {
  std::vector<WriteBuffer> frames;

  append_retransmission_frames(frames);

  while (true) {
    std::optional<Message> next_message = pop_next_message();
    if (!next_message.has_value()) {
      break;
    }

    Message queued_message = std::move(next_message.value());

    if (!is_outbound_publish_within_maximum_packet_size(queued_message)) {
      TRACE_GUARD(structured_tracer_, TraceLevel::Warning, "connection") {
        TraceEvent event;
        event.level = TraceLevel::Warning;
        event.module = "connection";
        event.info = "outbound_publish_dropped_maximum_packet_size";
        event.data.emplace_back("client_id", std::string(client_id_));
        event.data.emplace_back("topic", queued_message.topic.value);
        event.data.emplace_back("qos", std::to_string(static_cast<int>(queued_message.qos)));
        event.data.emplace_back("payload_bytes",
                                std::to_string(queued_message.payload.data.size()));
        event.data.emplace_back("maximum_packet_size",
                                std::to_string(maximum_packet_size_));
        structured_tracer_->emit(event);
      }
      continue;
    }

    if (queued_message.qos == QoS::AtMostOnce) {
      frames.push_back(
          encode_publish_packet(qos0_publish_from_message(queued_message)));
      continue;
    }

    if (!receive_maximum_.acquire()) {
      TRACE_GUARD(structured_tracer_, TraceLevel::Trace, "connection") {
        TraceEvent event;
        event.level = TraceLevel::Trace;
        event.module = "connection";
        event.info = "outbound_publish_deferred_receive_maximum";
        event.data.emplace_back("client_id", std::string(client_id_));
        event.data.emplace_back("topic", queued_message.topic.value);
        event.data.emplace_back("qos", std::to_string(static_cast<int>(queued_message.qos)));
        event.data.emplace_back("payload_bytes",
                                std::to_string(queued_message.payload.data.size()));
        structured_tracer_->emit(event);
      }
      deferred_messages_.push_front(std::move(queued_message));
      break;
    }

    if (queued_message.qos == QoS::AtLeastOnce) {
      const PublishPacket publish_packet =
          qos1_state_machine_.initiate_publish(queued_message);
      frames.push_back(encode_publish_packet(publish_packet));
      continue;
    }

    if (queued_message.qos == QoS::ExactlyOnce) {
      const PublishPacket publish_packet =
          qos2_state_machine_.initiate_publish(queued_message);
      frames.push_back(encode_publish_packet(publish_packet));
      continue;
    }

    throw std::invalid_argument(
        "ClientSession::drain_outbound: unsupported QoS");
  }

  return frames;
}

void ClientSession::append_retransmission_frames(
    std::vector<WriteBuffer> &frames) {
  const auto now = std::chrono::steady_clock::now();
  const std::vector<InflightEntry> entries =
      inflight_store_.entries_for(client_id_);

  for (const InflightEntry &entry : entries) {
    if (entry.direction != InflightDirection::Outbound) {
      continue;
    }
    const bool timeout_elapsed = (now - entry.timestamp) >= retransmit_timeout_;
    if (!replay_pending_inflight_ && !timeout_elapsed) {
      continue;
    }

    if (entry.qos == QoS::AtLeastOnce &&
        entry.state == InflightState::WaitingForPuback) {
      Message retransmit_message = entry.message;
      if (!MessageExpiryController::update_expiry(retransmit_message,
                                                  entry.timestamp, now)) {
        inflight_store_.remove(client_id_, entry.packet_id,
                               InflightDirection::Outbound);
        packet_id_manager_.release(entry.packet_id,
                                   InflightDirection::Outbound);
        continue;
      }

      const PublishPacket retransmitted_packet =
          qos1_state_machine_.retransmit(entry.packet_id);
      PublishPacket adjusted_packet = retransmitted_packet;
      adjusted_packet.properties = retransmit_message.properties;
      frames.push_back(encode_publish_packet(adjusted_packet));
      continue;
    }

    if (entry.qos == QoS::ExactlyOnce) {
      if (entry.state == InflightState::WaitingForPubrec) {
        Message retransmit_message = entry.message;
        if (!MessageExpiryController::update_expiry(retransmit_message,
                                                    entry.timestamp, now)) {
          inflight_store_.remove(client_id_, entry.packet_id,
                                 InflightDirection::Outbound);
          packet_id_manager_.release(entry.packet_id,
                                     InflightDirection::Outbound);
          continue;
        }

        const auto retransmitted_packet =
            qos2_state_machine_.retransmit(entry.packet_id);
        if (std::holds_alternative<PublishPacket>(retransmitted_packet)) {
          PublishPacket adjusted_packet =
              std::get<PublishPacket>(retransmitted_packet);
          adjusted_packet.properties = retransmit_message.properties;
          frames.push_back(encode_publish_packet(adjusted_packet));
        } else {
          frames.push_back(
              encode_pubrel_packet(std::get<PubrelPacket>(retransmitted_packet)));
        }
        continue;
      }

      const auto retransmitted_packet =
          qos2_state_machine_.retransmit(entry.packet_id);
      if (std::holds_alternative<PublishPacket>(retransmitted_packet)) {
        frames.push_back(encode_publish_packet(
            std::get<PublishPacket>(retransmitted_packet)));
      } else {
        frames.push_back(
            encode_pubrel_packet(std::get<PubrelPacket>(retransmitted_packet)));
      }
    }
  }

  replay_pending_inflight_ = false;
}

std::string_view ClientSession::client_id() const noexcept {
  return client_id_;
}

std::string_view ClientSession::username() const noexcept { return username_; }

std::string_view ClientSession::negotiated_auth_method() const noexcept {
  return enhanced_auth_handler_.auth_method();
}

ReceiveMaximum &ClientSession::receive_maximum() noexcept {
  return receive_maximum_;
}

TopicAliasTable &ClientSession::topic_alias_table() noexcept {
  return topic_alias_table_;
}

KeepAliveTimer &ClientSession::keep_alive_timer() noexcept {
  return keep_alive_timer_;
}

const KeepAliveTimer &ClientSession::keep_alive_timer() const noexcept {
  return keep_alive_timer_;
}

std::optional<std::chrono::steady_clock::time_point>
ClientSession::next_outbound_retransmit_deadline() const {
  if (replay_pending_inflight_) {
    return std::chrono::steady_clock::now();
  }

  const std::vector<InflightEntry> entries = inflight_store_.entries_for(client_id_);
  std::optional<std::chrono::steady_clock::time_point> next_deadline;
  for (const InflightEntry &entry : entries) {
    if (entry.direction != InflightDirection::Outbound) {
      continue;
    }

    const auto candidate_deadline = entry.timestamp + retransmit_timeout_;
    if (!next_deadline.has_value() || candidate_deadline < *next_deadline) {
      next_deadline = candidate_deadline;
    }
  }
  return next_deadline;
}

ConnectionStateMachine &ClientSession::connection_state_machine() noexcept {
  return connection_state_machine_;
}

std::shared_ptr<OutboundQueue> ClientSession::outbound_queue() const noexcept {
  return outbound_queue_;
}

Message
ClientSession::message_from_publish(const PublishPacket &publish_packet) {
  return Message{
      .topic = publish_packet.topic,
      .payload = publish_packet.payload,
      .qos = publish_packet.qos,
      .retain = publish_packet.retain,
      .properties = publish_packet.properties,
  };
}

PublishPacket ClientSession::qos0_publish_from_message(const Message &message) {
  return PublishPacket{
      .dup = false,
      .qos = QoS::AtMostOnce,
      .retain = message.retain,
      .topic = message.topic,
      .packet_id = std::nullopt,
      .payload = message.payload,
      .properties = message.properties,
  };
}

WriteBuffer ClientSession::encode_puback_packet(const PubackPacket &pkt) {
  WriteBuffer frame;
  encode_puback(frame, pkt);
  return frame;
}

WriteBuffer ClientSession::encode_pubrec_packet(const PubrecPacket &pkt) {
  WriteBuffer frame;
  encode_pubrec(frame, pkt);
  return frame;
}

WriteBuffer ClientSession::encode_pubrel_packet(const PubrelPacket &pkt) {
  WriteBuffer frame;
  encode_pubrel(frame, pkt);
  return frame;
}

WriteBuffer ClientSession::encode_pubcomp_packet(const PubcompPacket &pkt) {
  WriteBuffer frame;
  encode_pubcomp(frame, pkt);
  return frame;
}

WriteBuffer ClientSession::encode_publish_packet(const PublishPacket &pkt) {
  WriteBuffer frame;
  encode_publish(frame, pkt);
  return frame;
}

std::optional<Message> ClientSession::pop_next_message() {
  if (!deferred_messages_.empty()) {
    Message message = std::move(deferred_messages_.front());
    deferred_messages_.pop_front();
    return message;
  }

  return outbound_queue_->try_pop();
}

bool ClientSession::is_outbound_publish_within_maximum_packet_size(
    const Message &message) const {
  if (maximum_packet_size_ == 0U) {
    return true;
  }

  WriteBuffer encoded_frame;
  encode_publish(encoded_frame, publish_from_message_for_size_check(message));
  return encoded_frame.size() <= maximum_packet_size_;
}

PublishPacket
ClientSession::publish_from_message_for_size_check(const Message &message) {
  PublishPacket packet;
  packet.dup = false;
  packet.qos = message.qos;
  packet.retain = message.retain;
  packet.topic = message.topic;
  if (message.qos != QoS::AtMostOnce) {
    packet.packet_id = 1U;
  }
  packet.payload = message.payload;
  packet.properties = message.properties;
  return packet;
}

} // namespace mqtt
