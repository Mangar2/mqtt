#pragma once

/**
 * @file rs485_scheduler.h
 * @brief Tick-based RS485 scheduling over token exchange and send queue.
 */

#include "yaha/rs485_protocol/rs485_serial_protocol.h"
#include "yaha/rs485_state/rs485_send_queue.h"
#include "yaha/rs485_state/rs485_token_exchange.h"

#include <cstdint>
#include <functional>

namespace yaha {

inline constexpr std::uint32_t k_rs485_default_tick_delay_ms{100U};


/**
 * @brief Tick scheduler implementing legacy RS485 send/retry ordering.
 */
class Rs485Scheduler {
public:
    using SendCallback = std::function<void(const Rs485SerialMessage&)>;

    Rs485Scheduler(std::uint8_t myAddress, std::uint8_t maxVersion, std::uint32_t tickDelayMs);

    /**
     * @brief Sets send callback used for outbound serial frames.
     * @param callback Callback function.
     */
    void setSendCallback(SendCallback callback);

    /**
     * @brief Queues one outbound message.
     * @param message Message to queue.
     */
    void sendMessage(const Rs485SerialMessage& message);

    /**
     * @brief Processes one received serial message.
     * @param messageReceived Received serial message.
     * @return True when message should be forwarded to broker.
     */
    [[nodiscard]] bool processReceivedMessage(const Rs485SerialMessage& messageReceived);

    /**
     * @brief Processes one scheduler tick.
     */
    void processTick();

    /**
     * @brief Gets current retry counter.
     * @return Retry counter value.
     */
    [[nodiscard]] std::uint32_t sendRetryCount() const noexcept;

    /**
     * @brief Gets queued-message count.
     * @return Queue size.
     */
    [[nodiscard]] std::size_t queuedMessageCount() const noexcept;

    /**
     * @brief Gets sent-message counter.
     * @return Send counter.
     */
    [[nodiscard]] std::uint64_t sendCount() const noexcept;

    /**
     * @brief Gets scheduler tick delay setting.
     * @return Tick delay in ms.
     */
    [[nodiscard]] std::uint32_t tickDelayMs() const noexcept;

    /**
     * @brief Exposes token exchange for tests.
     * @return Mutable token exchange reference.
     */
    [[nodiscard]] Rs485TokenExchange& tokenExchange() noexcept;

private:
    void sendMessageInternal(const Rs485SerialMessage& message);
    void sendMessageFromQueue();

    Rs485TokenExchange tokenExchange_;
    std::uint32_t tickDelayMs_{k_rs485_default_tick_delay_ms};
    SendCallback sendCallback_{};
    Rs485SendQueue sendQueue_{};
    std::uint64_t sendCount_{0U};
    std::uint32_t sendRetryCount_{0U};
    std::uint64_t receivedCount_{0U};
    std::uint64_t tickCount_{0U};
};

} // namespace yaha
