#pragma once

/**
 * @file connack_properties.h
 * @brief Build broker-provided CONNACK properties from static configuration
 *        and CONNECT-driven request flags.
 */

#include <vector>

#include "broker/broker_config.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/property/property.h"

namespace mqtt {

/**
 * @brief Build the static CONNACK capability properties advertised by the broker.
 *
 * These properties are derived from broker configuration and feature support,
 * independent from request flags in a specific CONNECT packet.
 *
 * @param broker_config Broker runtime configuration.
 * @return Property list to include in successful CONNACK packets.
 */
[[nodiscard]] std::vector<Property> build_static_connack_properties(const BrokerConfig &broker_config);

/**
 * @brief Append CONNECT-driven optional CONNACK properties.
 *
 * Currently adds Response Information when the client requests it via
 * Request Response Information = 1.
 *
 * @param connect_packet Decoded CONNECT packet.
 * @param properties Mutable CONNACK property list to extend.
 */
void append_connect_driven_connack_properties(
    const ConnectPacket &connect_packet, std::vector<Property> &properties);

} // namespace mqtt