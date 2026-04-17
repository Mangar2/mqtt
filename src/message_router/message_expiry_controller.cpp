#include "message_router/message_expiry_controller.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

#include "data_model/property/property.h"
#include "data_model/property/property_id.h"

namespace mqtt {

bool MessageExpiryController::update_expiry(
    Message &msg, std::chrono::steady_clock::time_point enqueue_time,
    std::chrono::steady_clock::time_point now) {

  auto expiry_it =
      std::ranges::find_if(msg.properties, [](const Property &prop) {
        return prop.id == PropertyId::MessageExpiryInterval;
      });

  if (expiry_it == msg.properties.end()) {
    return true; // No expiry interval — message never expires.
  }

  const uint32_t interval_secs = std::get<uint32_t>(expiry_it->value);

  const auto elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(now - enqueue_time)
          .count();

  // Clamp elapsed to non-negative before comparing with interval.
  const auto elapsed_secs = static_cast<uint64_t>(elapsed > 0 ? elapsed : 0);

  if (elapsed_secs >= static_cast<uint64_t>(interval_secs)) {
    return false; // Message has expired — discard.
  }

  // Update the property to the remaining lifetime (12.4.3).
  const uint32_t remaining =
      interval_secs - static_cast<uint32_t>(elapsed_secs);
  expiry_it->value = remaining;

  return true;
}

} // namespace mqtt
