#include "client_api/client_config.h"

#include <string_view>

#include "data_model/property/property_id.h"

namespace mqtt {

namespace {

void validate_non_empty(std::string_view value, std::string_view field_name) {
  if (value.empty()) {
    throw ClientException(ClientError::InvalidPacket,
                          std::string(field_name) + " must not be empty");
  }
}

void validate_timeout(uint32_t timeout_ms, std::string_view field_name) {
  if (timeout_ms == 0U) {
    throw ClientException(ClientError::Timeout,
                          std::string(field_name) + " must be greater than zero");
  }
}

} // namespace

uint16_t default_port_for_transport(ClientTransportType transport) noexcept {
  switch (transport) {
  case ClientTransportType::Tcp:
    return 1883U;
  case ClientTransportType::WebSocket:
    return 80U;
  }
  return 1883U;
}

void validate_client_config_or_throw(const ClientConfig &client_config) {
  validate_non_empty(client_config.broker_host, "broker_host");
  validate_non_empty(client_config.client_id, "client_id");

  if (client_config.broker_port == 0U) {
    throw ClientException(ClientError::InvalidPacket,
                          "broker_port must be greater than zero");
  }

  if (!client_config.credentials.username.has_value() &&
      client_config.credentials.password.has_value()) {
    throw ClientException(ClientError::InvalidPacket,
                          "password requires username");
  }

  if (client_config.credentials.username.has_value() &&
      client_config.credentials.username->empty()) {
    throw ClientException(ClientError::InvalidPacket,
                          "username must not be empty when configured");
  }

  if (client_config.receive_maximum == 0U) {
    throw ClientException(ClientError::InvalidPacket,
                          "receive_maximum must be greater than zero");
  }

  validate_timeout(client_config.operation_timeouts.connect_ms,
                   "operation_timeouts.connect_ms");
  validate_timeout(client_config.operation_timeouts.publish_ms,
                   "operation_timeouts.publish_ms");
  validate_timeout(client_config.operation_timeouts.subscribe_ms,
                   "operation_timeouts.subscribe_ms");
  validate_timeout(client_config.operation_timeouts.unsubscribe_ms,
                   "operation_timeouts.unsubscribe_ms");
  validate_timeout(client_config.operation_timeouts.disconnect_ms,
                   "operation_timeouts.disconnect_ms");
}

ConnectPacket build_connect_packet(const ClientConfig &client_config) {
  validate_client_config_or_throw(client_config);

  ConnectPacket connect_packet;
  connect_packet.keep_alive = client_config.keep_alive_seconds;
  connect_packet.clean_start = client_config.clean_start;
  connect_packet.client_id = Utf8String{client_config.client_id};

  if (client_config.credentials.username.has_value()) {
    connect_packet.username = Utf8String{*client_config.credentials.username};
  }

  if (client_config.credentials.password.has_value()) {
    connect_packet.password =
        BinaryData::from_string(*client_config.credentials.password);
  }

  connect_packet.properties.push_back(
      Property{.id = PropertyId::ReceiveMaximum,
               .value = TwoByteInteger{client_config.receive_maximum}});
  connect_packet.properties.push_back(
      Property{.id = PropertyId::TopicAliasMaximum,
               .value = TwoByteInteger{client_config.topic_alias_maximum}});
  connect_packet.properties.push_back(
      Property{.id = PropertyId::SessionExpiryInterval,
               .value = FourByteInteger{
                   client_config.session_expiry_interval_seconds}});

  return connect_packet;
}

} // namespace mqtt
