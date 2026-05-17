#include "yaha/rs485_state/rs485_scheduler.h"

namespace yaha {
namespace {

constexpr std::uint32_t k_max_send_retry_count{10U};

} // namespace

Rs485Scheduler::Rs485Scheduler(
    const std::uint8_t myAddress,
    const std::uint8_t maxVersion,
    const std::uint32_t tickDelayMs)
    : tokenExchange_(myAddress, maxVersion),
      tickDelayMs_(tickDelayMs) {}

void Rs485Scheduler::setSendCallback(SendCallback callback) {
    sendCallback_ = std::move(callback);
}

void Rs485Scheduler::sendMessage(const Rs485SerialMessage& message) {
    sendQueue_.addMessage(message);
}

bool Rs485Scheduler::processReceivedMessage(const Rs485SerialMessage& messageReceived) {
    const auto tokenReply = tokenExchange_.processStateMessage(messageReceived);
    if (tokenReply.has_value()) {
        sendMessageInternal(*tokenReply);
    }

    const auto queuedMessage = sendQueue_.getMessage();
    if (queuedMessage.has_value() && isResponseMessage(*queuedMessage, messageReceived)) {
        sendQueue_.dequeue();
    }

    receivedCount_ += 1U;
    return messageReceived.command != k_rs485_token_command;
}

void Rs485Scheduler::processTick() {
    tickCount_ += 1U;

    const auto stateMessage = tokenExchange_.processStateNoMessage();
    if (stateMessage.has_value()) {
        sendMessageInternal(*stateMessage);
    }

    if (tokenExchange_.maySend()) {
        sendMessageFromQueue();

        // Prevent duplicate queue-send in same tick.
        tokenExchange_.setMaySend(false);
    }
}

std::uint32_t Rs485Scheduler::sendRetryCount() const noexcept {
    return sendRetryCount_;
}

std::size_t Rs485Scheduler::queuedMessageCount() const noexcept {
    std::size_t count = 0U;
    while (sendQueue_.getMessage(count).has_value()) {
        count += 1U;
    }
    return count;
}

std::uint64_t Rs485Scheduler::sendCount() const noexcept {
    return sendCount_;
}

std::uint32_t Rs485Scheduler::tickDelayMs() const noexcept {
    return tickDelayMs_;
}

Rs485TokenExchange& Rs485Scheduler::tokenExchange() noexcept {
    return tokenExchange_;
}

void Rs485Scheduler::sendMessageInternal(const Rs485SerialMessage& message) {
    Rs485SerialMessage outgoing = message;

    // Always send with currently negotiated oldest participant version.
    outgoing.version = tokenExchange_.version();

    if (static_cast<bool>(sendCallback_)) {
        sendCallback_(outgoing);
    }

    tokenExchange_.enableChangeVersion(outgoing);
    sendCount_ += 1U;
}

void Rs485Scheduler::sendMessageFromQueue() {
    const auto message = sendQueue_.getMessage();
    if (!message.has_value()) {
        return;
    }

    sendMessageInternal(*message);
    sendRetryCount_ += 1U;

    if (sendRetryCount_ >= k_max_send_retry_count || !message->reply) {
        sendQueue_.dequeue();
        sendRetryCount_ = 0U;
    }
}

} // namespace yaha
