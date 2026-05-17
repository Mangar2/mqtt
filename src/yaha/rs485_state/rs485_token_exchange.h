#pragma once

/**
 * @file rs485_token_exchange.h
 * @brief RS485 token message exchange logic.
 */

#include "yaha/rs485_protocol/rs485_serial_protocol.h"
#include "yaha/rs485_state/rs485_state.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yaha {

inline constexpr char k_rs485_token_command{'!'};

/**
 * @brief One address/version entry tracked by token exchange.
 */
struct Rs485AddressChainEntry {
    std::uint8_t address{0U};
    std::uint8_t version{0U};
};

/**
 * @brief Token exchange implementation on top of Rs485State.
 */
class Rs485TokenExchange {
public:
    Rs485TokenExchange(std::uint8_t myAddress, std::uint8_t maxVersion);

    /**
     * @brief Gets current negotiated send version.
     * @return Current protocol version.
     */
    [[nodiscard]] std::uint8_t version() const noexcept;

    /**
     * @brief Sets current negotiated send version.
     * @param value New version.
     */
    void setVersion(std::uint8_t value) noexcept;

    /**
     * @brief Gets may-send flag from state.
     * @return Current may-send flag.
     */
    [[nodiscard]] bool maySend() const noexcept;

    /**
     * @brief Sets may-send flag in state.
     * @param value New may-send flag.
     */
    void setMaySend(bool value) noexcept;

    /**
     * @brief Enables version negotiation after outgoing EnableSend.
     * @param messageSent Last sent token message.
     */
    void enableChangeVersion(const Rs485SerialMessage& messageSent);

    /**
     * @brief Processes one incoming serial message and emits optional token reply.
     * @param receivedMessage Incoming serial frame.
     * @return Optional outgoing token frame.
     */
    [[nodiscard]] std::optional<Rs485SerialMessage> processStateMessage(
        const Rs485SerialMessage& receivedMessage);

    /**
     * @brief Processes one scheduler tick without incoming serial message.
     * @return Optional outgoing token frame.
     */
    [[nodiscard]] std::optional<Rs485SerialMessage> processStateNoMessage();

    /**
     * @brief Returns state debug info text.
     * @return State debug line.
     */
    [[nodiscard]] std::string getStateInfo() const;

    /**
     * @brief Exposes state object for tests.
     * @return Const state reference.
     */
    [[nodiscard]] const Rs485State& state() const noexcept;

    /**
     * @brief Exposes tracked address chain for tests.
     * @return Ordered address chain entries.
     */
    [[nodiscard]] const std::vector<Rs485AddressChainEntry>& addressChain() const noexcept;

private:
    [[nodiscard]] bool isForMe(const Rs485SerialMessage& message) const noexcept;
    [[nodiscard]] std::uint8_t getReceiverAddress() const noexcept;

    [[nodiscard]] std::optional<Rs485SerialMessage> createStateSignalingMessage(
        Rs485StateResult stateUpdateResult) const;

    void updateRightSibling(std::uint8_t address);
    void updateLeftmostSibling(std::uint8_t address);
    void updateAddressChain(std::uint8_t address, std::uint8_t version);
    void updateVersion(const Rs485SerialMessage& message);

    std::uint8_t myAddress_{1U};
    std::uint8_t maxVersion_{1U};
    std::uint8_t version_{1U};
    bool mayChangeVersion_{false};
    Rs485State state_{};
    std::vector<Rs485AddressChainEntry> chain_{};
};

} // namespace yaha
