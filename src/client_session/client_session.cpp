#include "client_session/client_session.h"

#include <stdexcept>
#include <utility>

#include "codec/packet/publish_codec.h"

namespace mqtt {

ClientSession::ClientSession(std::string client_id, std::string username,
                             std::shared_ptr<IAuthenticator> authenticator,
                             std::shared_ptr<OutboundQueue> outbound_queue,
                             InflightStore &inflight_store,
                             uint16_t keep_alive_seconds,
                             uint16_t receive_maximum,
                             uint16_t topic_alias_maximum)
    : client_id_(std::move(client_id)), username_(std::move(username)),
      outbound_queue_(std::move(outbound_queue)), packet_id_manager_(),
      qos1_state_machine_(client_id_, packet_id_manager_, inflight_store),
      qos2_state_machine_(client_id_, packet_id_manager_, inflight_store),
      receive_maximum_(receive_maximum),
      topic_alias_table_(topic_alias_maximum),
      keep_alive_timer_(keep_alive_seconds),
      enhanced_auth_handler_(std::move(authenticator)) {
  if (!outbound_queue_) {
    throw std::invalid_argument(
        "ClientSession: outbound_queue must not be null");
  }
  connection_state_machine_.on_connect();
}

AuthResult ClientSession::initiate_auth(const ConnectPacket &connect_packet) {
  return enhanced_auth_handler_.initiate(connect_packet);
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

std::vector<WriteBuffer> ClientSession::drain_outbound() {
  std::vector<WriteBuffer> frames;

  while (true) {
    std::optional<Message> next_message = pop_next_message();
    if (!next_message.has_value()) {
      break;
    }

    Message queued_message = std::move(next_message.value());

    if (queued_message.qos == QoS::AtMostOnce) {
      frames.push_back(
          encode_publish_packet(qos0_publish_from_message(queued_message)));
      continue;
    }

    if (!receive_maximum_.acquire()) {
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

std::string_view ClientSession::client_id() const noexcept {
  return client_id_;
}

std::string_view ClientSession::username() const noexcept { return username_; }

ReceiveMaximum &ClientSession::receive_maximum() noexcept {
  return receive_maximum_;
}

TopicAliasTable &ClientSession::topic_alias_table() noexcept {
  return topic_alias_table_;
}

KeepAliveTimer &ClientSession::keep_alive_timer() noexcept {
  return keep_alive_timer_;
}

ConnectionStateMachine &ClientSession::connection_state_machine() noexcept {
  return connection_state_machine_;
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

} // namespace mqtt
