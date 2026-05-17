#include "yaha/rs485_state/rs485_token_exchange.h"

#include <algorithm>
#include <format>

namespace yaha {

Rs485TokenExchange::Rs485TokenExchange(const std::uint8_t myAddress, const std::uint8_t maxVersion)
    : myAddress_(myAddress),
      maxVersion_(maxVersion),
      version_(maxVersion) {}

std::uint8_t Rs485TokenExchange::version() const noexcept {
    return version_;
}

void Rs485TokenExchange::setVersion(const std::uint8_t value) noexcept {
    version_ = value;
}

bool Rs485TokenExchange::maySend() const noexcept {
    return state_.maySend();
}

void Rs485TokenExchange::setMaySend(const bool value) noexcept {
    state_.setMaySend(value);
}

void Rs485TokenExchange::enableChangeVersion(const Rs485SerialMessage& messageSent) {
    if (messageSent.value == static_cast<double>(static_cast<std::uint8_t>(Rs485StateResult::EnableSend))) {
        mayChangeVersion_ = true;
    }
}

std::optional<Rs485SerialMessage> Rs485TokenExchange::processStateMessage(
    const Rs485SerialMessage& receivedMessage) {
    if (receivedMessage.command != k_rs485_token_command) {
        return std::nullopt;
    }

    const bool messageForMe = isForMe(receivedMessage);
    updateAddressChain(receivedMessage.sender, receivedMessage.version);

    const auto stateValue = static_cast<std::uint8_t>(receivedMessage.value);
    const Rs485StateResult stateResult = state_.updateState(stateValue, !messageForMe);
    const auto stateSignalingMessage = createStateSignalingMessage(stateResult);

    updateVersion(receivedMessage);
    return stateSignalingMessage;
}

std::optional<Rs485SerialMessage> Rs485TokenExchange::processStateNoMessage() {
    const Rs485StateResult stateValue = state_.updateStateNoMessage();
    return createStateSignalingMessage(stateValue);
}

std::string Rs485TokenExchange::getStateInfo() const {
    const std::string leftText = state_.leftmostSibling().has_value()
        ? std::to_string(*state_.leftmostSibling())
        : "null";
    const std::string rightText = state_.rightSibling().has_value()
        ? std::to_string(*state_.rightSibling())
        : "null";

    return std::format(
        "State: {} Leftmost: {} Neighbour: {}",
        state_.getStateString(),
        leftText,
        rightText);
}

const Rs485State& Rs485TokenExchange::state() const noexcept {
    return state_;
}

const std::vector<Rs485AddressChainEntry>& Rs485TokenExchange::addressChain() const noexcept {
    return chain_;
}

bool Rs485TokenExchange::isForMe(const Rs485SerialMessage& message) const noexcept {
    return message.receiver == k_rs485_broadcast_address || message.receiver == myAddress_;
}

std::uint8_t Rs485TokenExchange::getReceiverAddress() const noexcept {
    if (state_.rightSibling().has_value()) {
        return *state_.rightSibling();
    }

    if (state_.leftmostSibling().has_value()) {
        return *state_.leftmostSibling();
    }

    return k_rs485_broadcast_address;
}

std::optional<Rs485SerialMessage> Rs485TokenExchange::createStateSignalingMessage(
    const Rs485StateResult stateUpdateResult) const {
    if (stateUpdateResult != Rs485StateResult::RegistrationInfo &&
        stateUpdateResult != Rs485StateResult::RegistrationRequest &&
        stateUpdateResult != Rs485StateResult::EnableSend) {
        return std::nullopt;
    }

    Rs485SerialMessage message{};
    message.version = version_;
    message.command = k_rs485_token_command;
    message.value = static_cast<double>(static_cast<std::uint8_t>(stateUpdateResult));
    message.sender = myAddress_;
    message.receiver = k_rs485_broadcast_address;
    message.reply = false;

    if (stateUpdateResult == Rs485StateResult::EnableSend) {
        message.receiver = getReceiverAddress();
    }

    return message;
}

void Rs485TokenExchange::updateRightSibling(const std::uint8_t address) {
    if (address <= myAddress_) {
        return;
    }

    if (!state_.rightSibling().has_value() || *state_.rightSibling() > address) {
        state_.setRightSibling(address);
    }
}

void Rs485TokenExchange::updateLeftmostSibling(const std::uint8_t address) {
    if (address >= myAddress_ || address == k_rs485_broadcast_address) {
        return;
    }

    if (!state_.leftmostSibling().has_value() || *state_.leftmostSibling() < address) {
        state_.setLeftmostSibling(address);
    }
}

void Rs485TokenExchange::updateAddressChain(const std::uint8_t address, const std::uint8_t version) {
    updateRightSibling(address);
    updateLeftmostSibling(address);

    // Preserve legacy JS quirk: Math.min(null, address) coerces null to 0.
    const std::uint8_t leftmostValue = state_.leftmostSibling().has_value() ? *state_.leftmostSibling() : 0U;
    state_.setLeftmostSibling(std::min(leftmostValue, address));

    const auto existing = std::ranges::find_if(chain_, [&](const Rs485AddressChainEntry& entry) {
        return entry.address == address;
    });
    if (existing != chain_.end()) {
        return;
    }

    const auto insertPos = std::ranges::find_if(chain_, [&](const Rs485AddressChainEntry& entry) {
        return entry.address > address;
    });
    chain_.insert(insertPos, Rs485AddressChainEntry{.address = address, .version = version});
}

void Rs485TokenExchange::updateVersion(const Rs485SerialMessage& message) {
    if (!mayChangeVersion_) {
        return;
    }

    if (static_cast<std::uint8_t>(message.value) == static_cast<std::uint8_t>(Rs485StateResult::EnableSend) &&
        message.version <= maxVersion_) {
        version_ = message.version;
    }
}

} // namespace yaha
