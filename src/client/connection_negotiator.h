#pragma once

/**
 * @file connection_negotiator.h
 * @brief Outbound client CONNECT/CONNACK negotiation helpers.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "client/client_error.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/property/property.h"
#include "network/tcp_connection.h"

namespace mqtt {

/**
 * @brief Negotiated session values extracted from broker CONNACK.
 */
struct ConnectionNegotiationResult {
  bool session_present{false};
  ReasonCode reason_code{ReasonCode::Success};
  uint16_t receive_maximum{65535U};
  uint16_t topic_alias_maximum{0U};
  std::optional<uint16_t> server_keep_alive;
  std::optional<std::string> assigned_client_id;
  std::vector<Property> connack_properties;
};

/**
 * @brief Client-side CONNECT/CONNACK negotiator.
 */
class ConnectionNegotiator {
public:
  /**
   * @brief Open a TCP connection to host:port.
   * @param host Hostname or numeric address.
   * @param port TCP port number.
   * @return Open TcpConnection on success.
   * @throws ClientException on resolution, socket, or connect failure.
   */
  [[nodiscard]] static TcpConnection dial_tcp(std::string_view host,
                                              uint16_t port);

  /**
   * @brief Send CONNECT and wait for one CONNACK.
   * @param connection Open TCP connection.
   * @param connect_packet CONNECT payload to send.
   * @param read_timeout_ms Read timeout for waiting response bytes.
   * @return Negotiated result extracted from CONNACK.
   * @throws ClientException on I/O timeout, protocol mismatch, or reject.
   */
  [[nodiscard]] static ConnectionNegotiationResult negotiate(
      TcpConnection &connection, const ConnectPacket &connect_packet,
      uint32_t read_timeout_ms = 5000U);
};

} // namespace mqtt
