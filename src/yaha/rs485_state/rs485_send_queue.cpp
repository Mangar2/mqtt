#include "yaha/rs485_state/rs485_send_queue.h"

namespace yaha {

void Rs485SendQueue::addMessage(const Rs485SerialMessage& message) {
    for (auto& queuedMessage : queue_) {
        if (isReplacingMessage(message, queuedMessage)) {
            queuedMessage = message;
            return;
        }
    }

    queue_.push_back(message);
}

void Rs485SendQueue::dequeue() {
    if (!queue_.empty()) {
        queue_.erase(queue_.begin());
    }
}

bool Rs485SendQueue::hasMessages() const noexcept {
    return !queue_.empty();
}

std::optional<Rs485SerialMessage> Rs485SendQueue::getMessage(const std::size_t index) const {
    if (index >= queue_.size()) {
        return std::nullopt;
    }

    return queue_[index];
}

bool Rs485SendQueue::isReplacingMessage(
    const Rs485SerialMessage& newMessage,
    const Rs485SerialMessage& queuedMessage) {
    return newMessage.sender == queuedMessage.sender &&
           newMessage.receiver == queuedMessage.receiver &&
           newMessage.command == queuedMessage.command &&
           newMessage.command != 'X';
}

bool isResponseMessage(
    const Rs485SerialMessage& queuedMessage,
    const Rs485SerialMessage& receivedMessage) noexcept {
    return queuedMessage.sender == receivedMessage.receiver &&
           queuedMessage.receiver == receivedMessage.sender &&
           queuedMessage.command == receivedMessage.command &&
           queuedMessage.value == receivedMessage.value;
}

} // namespace yaha
