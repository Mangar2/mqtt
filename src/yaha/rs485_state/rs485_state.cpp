#include "yaha/rs485_state/rs485_state.h"

#include <format>

namespace yaha {

Rs485State::Rs485State() = default;

Rs485StateResult Rs485State::updateState(const std::uint8_t request, const bool notForMe) {
    switch (state_) {
    case Rs485StateId::Unknown:
        return processUnknown(request, notForMe);
    case Rs485StateId::Reboot:
        return processReboot(request, notForMe);
    case Rs485StateId::Single:
        return processSingle(request, notForMe);
    case Rs485StateId::Unregistered:
        return processUnregistered(request, notForMe);
    case Rs485StateId::Registered:
        return processRegistered(request, notForMe);
    }

    return Rs485StateResult::Unchanged;
}

Rs485StateResult Rs485State::updateStateNoMessage() {
    Rs485StateResult result = Rs485StateResult::Unchanged;
    if (timer_ >= k_rs485_max_wait_timer) {
        result = updateState(k_rs485_loop_timeout, false);
    } else {
        const std::uint32_t loopState = timer_ % k_rs485_timer_loop;
        if (loopState == 0U) {
            result = updateState(k_rs485_loop_start, false);
        } else if (loopState == k_rs485_timer_small_period) {
            result = updateState(k_rs485_loop_short_break, false);
        } else if (loopState == k_rs485_timer_large_period) {
            result = updateState(k_rs485_loop_long_break, false);
        }
    }

    if (result != Rs485StateResult::StateChanged) {
        timer_ += 1U;
    }

    return result;
}

bool Rs485State::isRegistered() const noexcept {
    return state_ == Rs485StateId::Registered;
}

Rs485StateId Rs485State::state() const noexcept {
    return state_;
}

std::uint32_t Rs485State::timer() const noexcept {
    return timer_;
}

bool Rs485State::tokenLost() const noexcept {
    return tokenLost_;
}

std::string Rs485State::getStateString() const {
    switch (state_) {
    case Rs485StateId::Unknown:
        return "Unknown";
    case Rs485StateId::Reboot:
        return "Reboot";
    case Rs485StateId::Single:
        return "Single";
    case Rs485StateId::Unregistered:
        return "Unregistered";
    case Rs485StateId::Registered:
        return "Registered";
    }

    return "Undefined";
}

std::uint8_t Rs485State::getReceiverAddress() const noexcept {
    if (rightSibling_.has_value()) {
        return *rightSibling_;
    }
    if (leftmostSibling_.has_value()) {
        return *leftmostSibling_;
    }
    return k_rs485_broadcast_address;
}

Rs485StateResult Rs485State::calculateEnableSend() const noexcept {
    if (getReceiverAddress() == k_rs485_broadcast_address) {
        return Rs485StateResult::RegistrationRequest;
    }
    return Rs485StateResult::EnableSend;
}

void Rs485State::setRightSibling(const std::optional<std::uint8_t> value) noexcept {
    rightSibling_ = value;
}

std::optional<std::uint8_t> Rs485State::rightSibling() const noexcept {
    return rightSibling_;
}

void Rs485State::setLeftmostSibling(const std::optional<std::uint8_t> value) noexcept {
    leftmostSibling_ = value;
}

std::optional<std::uint8_t> Rs485State::leftmostSibling() const noexcept {
    return leftmostSibling_;
}

void Rs485State::setMaySend(const bool value) noexcept {
    maySend_ = value;
}

bool Rs485State::maySend() const noexcept {
    return maySend_;
}

void Rs485State::setTrace(const bool value) noexcept {
    trace_ = value;
}

bool Rs485State::trace() const noexcept {
    return trace_;
}

void Rs485State::setTraceLogCallback(TraceLogCallback callback) {
    traceLogCallback_ = std::move(callback);
}

void Rs485State::setState(const Rs485StateId newState) {
    timer_ = 0U;
    state_ = newState;

    if (trace_ && static_cast<bool>(traceLogCallback_)) {
        const std::string leftText = leftmostSibling_.has_value() ? std::to_string(*leftmostSibling_) : "null";
        const std::string rightText = rightSibling_.has_value() ? std::to_string(*rightSibling_) : "null";
        traceLogCallback_(std::format(
            "state changed to {}, leftmostSibling: {}, rightSibling: {}",
            getStateString(),
            leftText,
            rightText));
    }
}

Rs485StateResult Rs485State::processEnableSendWhenNotRegistered(const bool notForMe) {
    if (notForMe) {
        setState(Rs485StateId::Unregistered);
    } else {
        setState(Rs485StateId::Registered);
        maySend_ = true;
    }
    return Rs485StateResult::StateChanged;
}

Rs485StateResult Rs485State::processUnknown(const std::uint8_t request, const bool notForMe) {
    maySend_ = false;

    switch (request) {
    case static_cast<std::uint8_t>(Rs485StateResult::EnableSend):
        return processEnableSendWhenNotRegistered(notForMe);
    case static_cast<std::uint8_t>(Rs485StateResult::RegistrationInfo):
        return Rs485StateResult::Unchanged;
    case static_cast<std::uint8_t>(Rs485StateResult::RegistrationRequest):
        setState(Rs485StateId::Unregistered);
        return Rs485StateResult::RegistrationInfo;
    case k_rs485_loop_start:
        if (timer_ == 0U) {
            rightSibling_.reset();
            leftmostSibling_.reset();
        }
        return Rs485StateResult::Unchanged;
    case k_rs485_loop_timeout:
        setState(Rs485StateId::Reboot);
        return Rs485StateResult::StateChanged;
    default:
        return Rs485StateResult::Unchanged;
    }
}

Rs485StateResult Rs485State::processReboot(const std::uint8_t request, const bool notForMe) {
    maySend_ = false;

    switch (request) {
    case static_cast<std::uint8_t>(Rs485StateResult::EnableSend):
        return processEnableSendWhenNotRegistered(notForMe);
    case static_cast<std::uint8_t>(Rs485StateResult::RegistrationInfo):
        return Rs485StateResult::Unchanged;
    case static_cast<std::uint8_t>(Rs485StateResult::RegistrationRequest):
        setState(Rs485StateId::Unregistered);
        return Rs485StateResult::RegistrationInfo;
    case k_rs485_loop_start:
        return calculateEnableSend();
    case k_rs485_loop_timeout:
        setState(Rs485StateId::Single);
        return Rs485StateResult::StateChanged;
    default:
        return Rs485StateResult::Unchanged;
    }
}

Rs485StateResult Rs485State::processSingle(const std::uint8_t request, const bool notForMe) {
    switch (request) {
    case static_cast<std::uint8_t>(Rs485StateResult::EnableSend):
        return processEnableSendWhenNotRegistered(notForMe);
    case static_cast<std::uint8_t>(Rs485StateResult::RegistrationInfo):
        maySend_ = false;
        setState(Rs485StateId::Unknown);
        return Rs485StateResult::StateChanged;
    case static_cast<std::uint8_t>(Rs485StateResult::RegistrationRequest):
        maySend_ = false;
        setState(Rs485StateId::Unregistered);
        return Rs485StateResult::RegistrationInfo;
    case k_rs485_loop_start:
        maySend_ = false;
        return Rs485StateResult::RegistrationRequest;
    case k_rs485_loop_short_break:
        maySend_ = true;
        return Rs485StateResult::Unchanged;
    case k_rs485_loop_timeout:
        timer_ = 0U;
        return Rs485StateResult::Unchanged;
    default:
        return Rs485StateResult::Unchanged;
    }
}

Rs485StateResult Rs485State::processUnregistered(const std::uint8_t request, const bool notForMe) {
    maySend_ = false;

    switch (request) {
    case static_cast<std::uint8_t>(Rs485StateResult::EnableSend):
        if (!notForMe) {
            setState(Rs485StateId::Registered);
            maySend_ = true;
        }
        return Rs485StateResult::StateChanged;
    case static_cast<std::uint8_t>(Rs485StateResult::RegistrationInfo):
        return Rs485StateResult::Unchanged;
    case static_cast<std::uint8_t>(Rs485StateResult::RegistrationRequest):
        return Rs485StateResult::RegistrationInfo;
    case k_rs485_loop_timeout:
        setState(Rs485StateId::Unknown);
        return Rs485StateResult::StateChanged;
    default:
        return Rs485StateResult::Unchanged;
    }
}

Rs485StateResult Rs485State::registeredShortLoopBreak() {
    tokenLost_ = (lastEnableSend_ + k_rs485_timeout_no_enable_send <= timer_);
    if (timer_ == k_rs485_timer_small_period || tokenLost_) {
        lastEnableSend_ = timer_;
        maySend_ = false;
        if (!rightSibling_.has_value() && !tokenLost_) {
            return Rs485StateResult::RegistrationRequest;
        }
        return Rs485StateResult::EnableSend;
    }

    return Rs485StateResult::Unchanged;
}

Rs485StateResult Rs485State::processRegistered(const std::uint8_t request, const bool notForMe) {
    switch (request) {
    case static_cast<std::uint8_t>(Rs485StateResult::EnableSend):
        if (!notForMe) {
            maySend_ = true;
            timer_ = 0U;
        } else {
            maySend_ = false;
            lastEnableSend_ = timer_;
        }
        return Rs485StateResult::Unchanged;
    case static_cast<std::uint8_t>(Rs485StateResult::RegistrationInfo):
    case static_cast<std::uint8_t>(Rs485StateResult::RegistrationRequest):
        return Rs485StateResult::Unchanged;
    case k_rs485_loop_short_break:
        return registeredShortLoopBreak();
    case k_rs485_loop_long_break:
        if (timer_ == k_rs485_timer_large_period && !rightSibling_.has_value() && leftmostSibling_.has_value()) {
            return Rs485StateResult::EnableSend;
        }
        return Rs485StateResult::Unchanged;
    case k_rs485_loop_timeout:
        setState(Rs485StateId::Unregistered);
        return Rs485StateResult::StateChanged;
    default:
        return Rs485StateResult::Unchanged;
    }
}

} // namespace yaha
