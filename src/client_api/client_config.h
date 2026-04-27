#pragma once

/**
 * @file client_config.h
 * @brief Public MQTT client configuration object (Step 25).
 */

#include <cstdint>
#include <optional>
#include <string>

#include "client/client_error.h"
#include "client/reconnect_controller.h"
#include "data_model/packet/connect_packet.h"

namespace mqtt {

/**
 * @brief Transport type for outbound broker connection.
 */
enum class ClientTransportType : uint8_t {
  Tcp,
  WebSocket,
};

/**
 * @brief Optional username/password credentials.
 */
struct ClientCredentials {
  std::optional<std::string> username;
  std::optional<std::string> password;
};

/**
 * @brief Default timeout set for each public operation type.
 */
struct ClientOperationTimeouts {
  uint32_t connect_ms{5000U};
  uint32_t publish_ms{5000U};
  uint32_t subscribe_ms{5000U};
  uint32_t unsubscribe_ms{5000U};
  uint32_t disconnect_ms{5000U};
};

/**
 * @brief Unified public MQTT client configuration.
 */
struct ClientConfig {
  std::string broker_host{"127.0.0.1"};
  uint16_t broker_port{1883U};
  ClientTransportType transport{ClientTransportType::Tcp};

  std::string client_id{"mqtt-client"};
  ClientCredentials credentials{};

  bool clean_start{true};
  uint16_t keep_alive_seconds{30U};
  uint32_t session_expiry_interval_seconds{0U};
  uint16_t receive_maximum{65535U};
  uint16_t topic_alias_maximum{0U};

  ReconnectBackoffPolicy reconnect_backoff{};
  ClientOperationTimeouts operation_timeouts{};
};

/**
 * @brief Return default port for the selected transport type.
 * @param transport Transport type.
 * @return Default broker port.
 */
[[nodiscard]] uint16_t
default_port_for_transport(ClientTransportType transport) noexcept;

/**
 * @brief Validate configuration and throw on invalid values.
 * @param client_config Configuration to validate.
 * @throws ClientException when required fields or ranges are invalid.
 */
void validate_client_config_or_throw(const ClientConfig &client_config);

/**
 * @brief Build CONNECT packet model from configuration fields.
 * @param client_config Source configuration.
 * @return CONNECT packet payload model.
 * @throws ClientException when configuration is invalid.
 */
[[nodiscard]] ConnectPacket
build_connect_packet(const ClientConfig &client_config);

} // namespace mqtt
