#pragma once

/**
 * @file rs485_send_queue.h
 * @brief RS485 outbound queue with legacy replacement semantics.
 */

#include "yaha/rs485_protocol/rs485_serial_protocol.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace yaha {

/**
 * @brief FIFO queue for outbound RS485 messages.
 */
class Rs485SendQueue {
public:
    /**
     * @brief Adds a message and replaces existing entry when allowed.
     * @param message Message to enqueue.
     */
    void addMessage(const Rs485SerialMessage& message);

    /**
     * @brief Removes front message when queue is not empty.
     */
    void dequeue();

    /**
     * @brief Returns true when queue has at least one message.
     * @return True when queue non-empty.
     */
    [[nodiscard]] bool hasMessages() const noexcept;

    /**
     * @brief Gets one message from queue.
     * @param index Queue index.
     * @return Message when index is valid.
     */
    [[nodiscard]] std::optional<Rs485SerialMessage> getMessage(std::size_t index = 0U) const;

private:
    [[nodiscard]] static bool isReplacingMessage(
        const Rs485SerialMessage& newMessage,
        const Rs485SerialMessage& queuedMessage);

    std::vector<Rs485SerialMessage> queue_{};
};

/**
 * @brief Checks whether received message is response to queued message.
 * @param queuedMessage Original sent message.
 * @param receivedMessage Received message.
 * @return True when response predicate matches.
 */
[[nodiscard]] bool isResponseMessage(
    const Rs485SerialMessage& queuedMessage,
    const Rs485SerialMessage& receivedMessage) noexcept;

} // namespace yaha
