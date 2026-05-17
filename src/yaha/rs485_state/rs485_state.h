#pragma once

/**
 * @file rs485_state.h
 * @brief RS485 token state machine implementation.
 */

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace yaha {

enum class Rs485StateId : std::uint8_t {
    Unknown = 0U,
    Reboot = 1U,
    Single = 2U,
    Unregistered = 3U,
    Registered = 4U,
};

enum class Rs485StateResult : std::uint8_t {
    Unchanged = 0U,
    EnableSend = 1U,
    RegistrationInfo = 2U,
    RegistrationRequest = 3U,
    StateChanged = 4U,
};

inline constexpr std::uint8_t k_rs485_broadcast_address{0U};
inline constexpr std::uint32_t k_rs485_max_wait_timer{100U};
inline constexpr std::uint32_t k_rs485_timer_small_period{3U};
inline constexpr std::uint32_t k_rs485_timer_large_period{7U};
inline constexpr std::uint32_t k_rs485_timer_loop{k_rs485_timer_small_period + k_rs485_timer_large_period};
inline constexpr std::uint32_t k_rs485_timeout_no_enable_send{4U * k_rs485_timer_loop};

inline constexpr std::uint8_t k_rs485_loop_timeout{10U};
inline constexpr std::uint8_t k_rs485_loop_start{11U};
inline constexpr std::uint8_t k_rs485_loop_short_break{12U};
inline constexpr std::uint8_t k_rs485_loop_long_break{13U};

/**
 * @brief Token state machine for RS485 bus arbitration.
 */
class Rs485State {
public:
    using TraceLogCallback = std::function<void(const std::string&)>;

    Rs485State();

    /**
     * @brief Updates state with one incoming state request.
     * @param request Token/loop request value.
     * @param notForMe True when request is not addressed to this node.
     * @return State output command.
     */
    [[nodiscard]] Rs485StateResult updateState(std::uint8_t request, bool notForMe);

    /**
     * @brief Updates state for one tick without incoming message.
     * @return State output command.
     */
    [[nodiscard]] Rs485StateResult updateStateNoMessage();

    /**
     * @brief Returns true if current state is Registered.
     * @return True when state is Registered.
     */
    [[nodiscard]] bool isRegistered() const noexcept;

    /**
     * @brief Returns current state value.
     * @return Current state enum.
     */
    [[nodiscard]] Rs485StateId state() const noexcept;

    /**
     * @brief Returns current timer value.
     * @return Internal timer ticks.
     */
    [[nodiscard]] std::uint32_t timer() const noexcept;

    /**
     * @brief Gets runtime token-lost flag.
     * @return True when token-loss threshold was hit in registered short break logic.
     */
    [[nodiscard]] bool tokenLost() const noexcept;

    /**
     * @brief Gets string view of current state name.
     * @return State name text.
     */
    [[nodiscard]] std::string getStateString() const;

    /**
     * @brief Gets next token receiver address according to sibling rules.
     * @return Receiver address.
     */
    [[nodiscard]] std::uint8_t getReceiverAddress() const noexcept;

    /**
     * @brief Calculates EnableSend/RegistrationRequest output by receiver availability.
     * @return Calculated state output command.
     */
    [[nodiscard]] Rs485StateResult calculateEnableSend() const noexcept;

    /**
     * @brief Sets right sibling address.
     * @param value Optional right sibling address.
     */
    void setRightSibling(std::optional<std::uint8_t> value) noexcept;

    /**
     * @brief Gets right sibling address.
     * @return Optional right sibling.
     */
    [[nodiscard]] std::optional<std::uint8_t> rightSibling() const noexcept;

    /**
     * @brief Sets leftmost sibling address.
     * @param value Optional leftmost sibling address.
     */
    void setLeftmostSibling(std::optional<std::uint8_t> value) noexcept;

    /**
     * @brief Gets leftmost sibling address.
     * @return Optional leftmost sibling.
     */
    [[nodiscard]] std::optional<std::uint8_t> leftmostSibling() const noexcept;

    /**
     * @brief Sets may-send flag.
     * @param value New may-send flag.
     */
    void setMaySend(bool value) noexcept;

    /**
     * @brief Gets may-send flag.
     * @return Current may-send flag.
     */
    [[nodiscard]] bool maySend() const noexcept;

    /**
     * @brief Enables or disables trace logging in setState side effect.
     * @param value Trace flag.
     */
    void setTrace(bool value) noexcept;

    /**
     * @brief Gets trace enabled flag.
     * @return Trace flag.
     */
    [[nodiscard]] bool trace() const noexcept;

    /**
     * @brief Sets callback used for trace side-effect messages.
     * @param callback Trace logger callback.
     */
    void setTraceLogCallback(TraceLogCallback callback);

private:
    void setState(Rs485StateId newState);

    [[nodiscard]] Rs485StateResult processEnableSendWhenNotRegistered(bool notForMe);
    [[nodiscard]] Rs485StateResult processUnknown(std::uint8_t request, bool notForMe);
    [[nodiscard]] Rs485StateResult processReboot(std::uint8_t request, bool notForMe);
    [[nodiscard]] Rs485StateResult processSingle(std::uint8_t request, bool notForMe);
    [[nodiscard]] Rs485StateResult processUnregistered(std::uint8_t request, bool notForMe);
    [[nodiscard]] Rs485StateResult registeredShortLoopBreak();
    [[nodiscard]] Rs485StateResult processRegistered(std::uint8_t request, bool notForMe);

    Rs485StateId state_{Rs485StateId::Unknown};
    std::uint32_t timer_{0U};
    std::uint32_t lastEnableSend_{0U};
    std::optional<std::uint8_t> rightSibling_{};
    std::optional<std::uint8_t> leftmostSibling_{};
    bool maySend_{false};
    bool trace_{false};
    bool tokenLost_{false};
    TraceLogCallback traceLogCallback_{};
};

} // namespace yaha
