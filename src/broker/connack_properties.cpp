/**
 * @file connack_properties.cpp
 * @brief CONNACK property assembly helpers for broker handshake responses.
 */

#include "broker/connack_properties.h"

#include "data_model/property/property_id.h"

namespace mqtt {

namespace {

constexpr uint8_t k_retain_available = 1U;
constexpr FourByteInteger k_maximum_packet_size = 0x0FFFFFFFU;
constexpr uint8_t k_wildcard_subscription_available = 1U;
constexpr uint8_t k_subscription_identifier_available = 1U;
constexpr uint8_t k_shared_subscription_available = 1U;

[[nodiscard]] bool
requests_response_information(const std::vector<Property> &properties) {
  for (const Property &property : properties) {
    if (property.id != PropertyId::RequestResponseInformation) {
      continue;
    }
    const auto *request_ptr = std::get_if<uint8_t>(&property.value);
    return request_ptr != nullptr && *request_ptr == 1U;
  }
  return false;
}

} // namespace

std::vector<Property>
build_static_connack_properties(const BrokerConfig &broker_config) {
  std::vector<Property> properties;
  properties.push_back({PropertyId::ReceiveMaximum,
                        TwoByteInteger{broker_config.receive_maximum}});
  if (broker_config.server_keep_alive > 0U) {
    properties.push_back({PropertyId::ServerKeepAlive,
                          TwoByteInteger{broker_config.server_keep_alive}});
  }
  properties.push_back({PropertyId::RetainAvailable, k_retain_available});
  properties.push_back({PropertyId::MaximumPacketSize, k_maximum_packet_size});
  properties.push_back({PropertyId::TopicAliasMaximum,
                        TwoByteInteger{broker_config.topic_alias_maximum}});
  properties.push_back({PropertyId::WildcardSubscriptionAvailable,
                        k_wildcard_subscription_available});
  properties.push_back({PropertyId::SubscriptionIdentifierAvailable,
                        k_subscription_identifier_available});
  properties.push_back({PropertyId::SharedSubscriptionAvailable,
                        k_shared_subscription_available});
  return properties;
}

void append_connect_driven_connack_properties(
    const ConnectPacket &connect_packet, std::vector<Property> &properties) {
  if (requests_response_information(connect_packet.properties)) {
    properties.push_back(
        Property{.id = PropertyId::ResponseInformation,
                 .value = Utf8String{"broker/response-information"}});
  }
}

} // namespace mqtt
