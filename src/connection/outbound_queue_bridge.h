#pragma once

/**
 * @file outbound_queue_bridge.h
 * @brief Helpers for moving pending messages between outbound delivery queues
 *        (Module 24).
 */

#include <cstddef>
#include <vector>

#include "data_model/message/message.h"
#include "outbound_queue/outbound_queue.h"

namespace mqtt {

/**
 * @brief Drain all pending messages from an outbound queue.
 *
 * Messages are returned in FIFO order. The source queue is empty afterwards.
 *
 * @param source Queue to drain.
 * @return All drained messages in FIFO order.
 */
[[nodiscard]] std::vector<Message> drain_pending_outbound_messages(OutboundQueue &source);

/**
 * @brief Move pending messages from one outbound queue to another.
 *
 * Stops when the source queue is empty or target push rejects a message.
 *
 * @param source Queue to drain from.
 * @param target Queue to push into.
 * @return Number of messages successfully moved to target.
 */
[[nodiscard]] std::size_t transfer_pending_outbound_messages(
    OutboundQueue &source, OutboundQueue &target);

} // namespace mqtt
