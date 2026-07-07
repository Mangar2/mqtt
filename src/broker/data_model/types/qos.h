#pragma once

/**
 * @file qos.h
 * @brief MQTT 5.0 Quality of Service levels (Section 4.3).
 */

#include <cstdint>

namespace mqtt {

/**
 * @brief MQTT 5.0 Quality of Service levels.
 *
 * Governs message delivery guarantees between client and broker.
 */
enum class QoS : uint8_t {
    AtMostOnce  = 0,  ///< Fire and forget — no acknowledgement.
    AtLeastOnce = 1,  ///< Acknowledged delivery — duplicates possible.
    ExactlyOnce = 2,  ///< Assured delivery — no duplicates.
};

} // namespace mqtt
