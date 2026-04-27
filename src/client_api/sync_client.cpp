#include "client_api/sync_client.h"

#include <utility>

namespace mqtt {

SyncClient::SyncClient(std::string client_id,
                       ReconnectBackoffPolicy reconnect_backoff)
    : client_id_(std::move(client_id)),
      subscription_manager_(client_id_),
      session_state_keeper_(client_id_, 0U),
      reconnect_controller_(reconnect_backoff) {
  if (client_id_.empty()) {
    throw ClientException(ClientError::InvalidPacket,
                          "sync client requires non-empty client id");
  }
}

void SyncClient::set_callbacks(SyncClientCallbacks callbacks) noexcept {
  callbacks_ = std::move(callbacks);
}

ConnectionNegotiationResult SyncClient::connect(const ConnectPacket &connect_packet,
                                                uint32_t timeout_ms) {
  require_callback(static_cast<bool>(callbacks_.connect_and_negotiate),
                   "connect", ClientError::ProtocolError);

  const ConnectionNegotiationResult negotiation_result =
      callbacks_.connect_and_negotiate(connect_packet, timeout_ms);

  connected_ = true;
  reconnect_controller_.mark_connected();

  if (connect_packet.clean_start || !negotiation_result.session_present) {
    subscription_manager_.clear();
    publish_pipeline_.clear();
    session_state_keeper_.clear_subscriptions();
    session_state_keeper_.set_outbound_inflight({});
  }

  return negotiation_result;
}

ReasonCode SyncClient::publish(const Message &message, uint32_t timeout_ms) {
  require_connected();
  require_callback(static_cast<bool>(callbacks_.send_publish), "publish/send",
                   ClientError::WriteFailed);

  const ClientPublishPipeline::PublishStartResult publish_start_result =
      publish_pipeline_.begin_publish(message);
  callbacks_.send_publish(publish_start_result.publish_packet);

  if (publish_start_result.completed) {
    return ReasonCode::Success;
  }

  if (!publish_start_result.packet_id.has_value()) {
    throw ClientException(ClientError::ProtocolError,
                          "tracked publish is missing packet id");
  }

  const uint16_t packet_id = *publish_start_result.packet_id;
  if (publish_start_result.publish_packet.qos == QoS::AtLeastOnce) {
    require_callback(static_cast<bool>(callbacks_.wait_puback),
                     "publish/wait_puback", ClientError::Timeout);
    const PubackPacket puback_packet = callbacks_.wait_puback(packet_id, timeout_ms);
    const ClientPublishPipeline::PublishAckResult ack_result =
        publish_pipeline_.on_puback(puback_packet);
    return ack_result.reason_code;
  }

  require_callback(static_cast<bool>(callbacks_.wait_pubrec),
                   "publish/wait_pubrec", ClientError::Timeout);
  const PubrecPacket pubrec_packet = callbacks_.wait_pubrec(packet_id, timeout_ms);
  const ClientPublishPipeline::PublishAckResult pubrec_result =
      publish_pipeline_.on_pubrec(pubrec_packet);
  if (pubrec_result.completed) {
    return pubrec_result.reason_code;
  }

  if (pubrec_result.send_pubrel) {
    require_callback(static_cast<bool>(callbacks_.send_pubrel),
                     "publish/send_pubrel", ClientError::WriteFailed);
    if (!pubrec_result.pubrel_packet.has_value()) {
      throw ClientException(ClientError::ProtocolError,
                            "PUBREL send requested but packet is missing");
    }
    callbacks_.send_pubrel(*pubrec_result.pubrel_packet);
  }

  require_callback(static_cast<bool>(callbacks_.wait_pubcomp),
                   "publish/wait_pubcomp", ClientError::Timeout);
  const PubcompPacket pubcomp_packet =
      callbacks_.wait_pubcomp(packet_id, timeout_ms);
  const ClientPublishPipeline::PublishAckResult pubcomp_result =
      publish_pipeline_.on_pubcomp(pubcomp_packet);
  return pubcomp_result.reason_code;
}

std::vector<ReasonCode> SyncClient::subscribe(
    const std::vector<ClientSubscriptionManager::SubscribeRequest> &requests,
    uint32_t timeout_ms) {
  require_connected();
  require_callback(static_cast<bool>(callbacks_.send_subscribe),
                   "subscribe/send", ClientError::WriteFailed);
  require_callback(static_cast<bool>(callbacks_.wait_suback),
                   "subscribe/wait_suback", ClientError::Timeout);

  const ClientSubscriptionManager::SubscribeOperation subscribe_operation =
      subscription_manager_.begin_subscribe(requests);
  callbacks_.send_subscribe(subscribe_operation.packet);
  const SubackPacket suback_packet =
      callbacks_.wait_suback(subscribe_operation.packet_id, timeout_ms);
  const ClientSubscriptionManager::AckResult ack_result =
      subscription_manager_.on_suback(suback_packet);

  for (std::size_t request_index = 0U;
       request_index < requests.size() && request_index < ack_result.reason_codes.size();
       ++request_index) {
    const std::optional<QoS> granted_qos =
        decode_granted_qos(ack_result.reason_codes[request_index]);
    if (!granted_qos.has_value()) {
      continue;
    }
    Subscription subscription;
    subscription.topic_filter = Utf8String{requests[request_index].topic_filter};
    subscription.qos = *granted_qos;
    subscription.options = requests[request_index].options;
    session_state_keeper_.upsert_subscription(subscription);
  }

  return ack_result.reason_codes;
}

std::vector<ReasonCode>
SyncClient::unsubscribe(const std::vector<std::string> &topic_filters,
                       uint32_t timeout_ms) {
  require_connected();
  require_callback(static_cast<bool>(callbacks_.send_unsubscribe),
                   "unsubscribe/send", ClientError::WriteFailed);
  require_callback(static_cast<bool>(callbacks_.wait_unsuback),
                   "unsubscribe/wait_unsuback", ClientError::Timeout);

  const ClientSubscriptionManager::UnsubscribeOperation unsubscribe_operation =
      subscription_manager_.begin_unsubscribe(topic_filters);
  callbacks_.send_unsubscribe(unsubscribe_operation.packet);
  const UnsubackPacket unsuback_packet =
      callbacks_.wait_unsuback(unsubscribe_operation.packet_id, timeout_ms);
  const ClientSubscriptionManager::AckResult ack_result =
      subscription_manager_.on_unsuback(unsuback_packet);

  for (std::size_t filter_index = 0U;
       filter_index < topic_filters.size() &&
       filter_index < ack_result.reason_codes.size();
       ++filter_index) {
    if (is_error(ack_result.reason_codes[filter_index])) {
      continue;
    }
    const bool removed_from_snapshot =
        session_state_keeper_.remove_subscription(topic_filters[filter_index]);
    if (!removed_from_snapshot) {
      // The active subscription table is the source of truth. Snapshot absence
      // is tolerated for idempotent unsubscribe behavior.
    }
  }

  return ack_result.reason_codes;
}

void SyncClient::disconnect(ReasonCode reason_code) {
  if (!connected_) {
    reconnect_controller_.on_connection_lost(ReconnectTrigger::UserInitiated);
    return;
  }

  DisconnectPacket disconnect_packet;
  disconnect_packet.reason_code = reason_code;

  if (callbacks_.send_disconnect) {
    callbacks_.send_disconnect(disconnect_packet);
  }

  connected_ = false;
  reconnect_controller_.on_connection_lost(ReconnectTrigger::UserInitiated);
  publish_pipeline_.clear();
}

bool SyncClient::is_connected() const noexcept { return connected_; }

bool SyncClient::has_subscription(std::string_view topic_filter) const noexcept {
  return subscription_manager_.has_subscription(topic_filter);
}

std::size_t
SyncClient::dispatch_inbound_publish(const PublishPacket &publish_packet) const {
  return subscription_manager_.dispatch_inbound_publish(publish_packet);
}

void SyncClient::require_connected() const {
  if (!connected_) {
    throw ClientException(ClientError::ProtocolError,
                          "sync client is not connected");
  }
}

void SyncClient::require_callback(bool has_callback, std::string_view operation,
                                  ClientError error_code) {
  if (!has_callback) {
    throw ClientException(error_code,
                          std::string("missing callback for operation ") +
                              std::string(operation));
  }
}

std::optional<QoS> SyncClient::decode_granted_qos(ReasonCode reason_code) noexcept {
  switch (reason_code) {
  case ReasonCode::Success:
    return QoS::AtMostOnce;
  case ReasonCode::GrantedQoS1:
    return QoS::AtLeastOnce;
  case ReasonCode::GrantedQoS2:
    return QoS::ExactlyOnce;
  default:
    return std::nullopt;
  }
}

} // namespace mqtt
