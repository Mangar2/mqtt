#pragma once

/**
 * @file inflight_direction.h
 * @brief Direction of an in-flight QoS 1 / QoS 2 message exchange (Module 1.7.2).
 */

#include <cstdint>

namespace mqtt {

/**
 * @brief Indicates whether a QoS handshake is inbound or outbound (Module 1.7.2).
 *
 * - Inbound:  the broker received a PUBLISH from a client and is sending acknowledgements.
 * - Outbound: the broker is forwarding a PUBLISH to a client and awaiting acknowledgements.
 */
enum class InflightDirection : uint8_t {
    Inbound  = 0,  ///< Message received from a publishing client; broker is acknowledging.
    Outbound = 1,  ///< Message being sent to a subscribing client; client is acknowledging.
};

} // namespace mqtt
