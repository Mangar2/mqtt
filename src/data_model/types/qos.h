#pragma once

#include <cstdint>

namespace mqtt {

// MQTT 5.0 Quality of Service levels (Section 4.3).
enum class QoS : uint8_t {
    AtMostOnce  = 0,  // Fire and forget — no acknowledgement
    AtLeastOnce = 1,  // Acknowledged delivery — duplicates possible
    ExactlyOnce = 2,  // Assured delivery — no duplicates
};

} // namespace mqtt
