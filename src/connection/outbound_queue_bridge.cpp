/**
 * @file outbound_queue_bridge.cpp
 * @brief Helpers for moving pending outbound messages (Module 24).
 */

#include "connection/outbound_queue_bridge.h"

#include <utility>

namespace mqtt {

std::vector<Message> drain_pending_outbound_messages(OutboundQueue &source) {
  std::vector<Message> drained_messages;

  while (true) {
    std::optional<Message> pending_message = source.try_pop();
    if (!pending_message.has_value()) {
      break;
    }
    drained_messages.push_back(std::move(*pending_message));
  }

  return drained_messages;
}

std::size_t transfer_pending_outbound_messages(OutboundQueue &source,
                                               OutboundQueue &target) {
  std::size_t moved_count = 0U;

  while (true) {
    std::optional<Message> pending_message = source.try_pop();
    if (!pending_message.has_value()) {
      break;
    }

    if (!target.push(std::move(*pending_message))) {
      break;
    }

    ++moved_count;
  }

  return moved_count;
}

} // namespace mqtt
