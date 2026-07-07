#include "yaha/automation_client_rule_runtime/rule_delivery_control.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <variant>

#include "yaha/automation_client_rule_runtime/rule_field_access.h"

namespace yaha {
namespace {

constexpr double k_zero_epsilon{1e-12};
constexpr std::size_t k_iso_min_length{20U};
constexpr std::size_t k_iso_year_sep_index{4U};
constexpr std::size_t k_iso_month_sep_index{7U};
constexpr std::size_t k_iso_date_time_sep_index{10U};
constexpr std::size_t k_iso_hour_sep_index{13U};
constexpr std::size_t k_iso_minute_sep_index{16U};
constexpr std::size_t k_iso_fraction_dot_index{19U};
constexpr std::size_t k_iso_min_fraction_length{22U};
constexpr std::size_t k_iso_fraction_first_digit_index{20U};

[[nodiscard]] bool isDigitAt(const std::string& textValue, const std::size_t indexValue) {
    if (indexValue >= textValue.size()) {
        return false;
    }
    return std::isdigit(static_cast<unsigned char>(textValue[indexValue])) != 0;
}

[[nodiscard]] bool isIsoUtcTimestampString(const std::string& textValue) {
    if (textValue.size() < k_iso_min_length) {
        return false;
    }

    if (textValue[k_iso_year_sep_index] != '-'
        || textValue[k_iso_month_sep_index] != '-'
        || textValue[k_iso_date_time_sep_index] != 'T'
        || textValue[k_iso_hour_sep_index] != ':'
        || textValue[k_iso_minute_sep_index] != ':'
        || textValue.back() != 'Z') {
        return false;
    }

    const std::array<std::size_t, 14U> digitPositions{
        0U, 1U, 2U, 3U,
        5U, 6U,
        8U, 9U,
        11U, 12U,
        14U, 15U,
        17U, 18U};
    if (!std::ranges::all_of(digitPositions, [&textValue](const std::size_t indexValue) {
            return isDigitAt(textValue, indexValue);
        })) {
        return false;
    }

    if (textValue.size() == k_iso_min_length) {
        return true;
    }

    if (textValue[k_iso_fraction_dot_index] != '.') {
        return false;
    }

    if (textValue.size() < k_iso_min_fraction_length) {
        return false;
    }

    for (std::size_t indexValue = k_iso_fraction_first_digit_index;
         indexValue + 1U < textValue.size();
         ++indexValue) {
        if (!isDigitAt(textValue, indexValue)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::string deliveryComparableHash(
    const Message& candidateMessage,
    const bool cooldownConfigured) {
    if (!cooldownConfigured) {
        return candidateMessage.topic() + "|" + valueToStableText(candidateMessage.value());
    }

    if (std::holds_alternative<std::string>(candidateMessage.value())) {
        const auto& valueText = std::get<std::string>(candidateMessage.value());
        if (isIsoUtcTimestampString(valueText)) {
            return candidateMessage.topic() + "|t:iso-utc-timestamp";
        }
    }

    return candidateMessage.topic() + "|" + valueToStableText(candidateMessage.value());
}

} // namespace

std::string valueToStableText(const Value& messageValue) {
    if (std::holds_alternative<std::string>(messageValue)) {
        return "s:" + std::get<std::string>(messageValue);
    }

    std::ostringstream textStream;
    textStream << std::get<double>(messageValue);
    return "n:" + textStream.str();
}

bool isZeroPayloadValue(const Value& messageValue) {
    if (std::holds_alternative<double>(messageValue)) {
        return std::fabs(std::get<double>(messageValue)) <= k_zero_epsilon;
    }

    const auto& valueText = std::get<std::string>(messageValue);
    return valueText == "0";
}

std::optional<double> readPositiveGateSeconds(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName) {
    const auto value = readNumberField(ruleObject, fieldName);
    if (!value.has_value()) {
        return std::nullopt;
    }
    if (*value <= 0.0) {
        return std::nullopt;
    }
    return value;
}

std::vector<Message> applyDeliveryControls(
    const std::string& rulePath,
    const RuleTreeNode::Object& ruleObject,
    const std::vector<Message>& candidateMessages,
    const std::chrono::system_clock::time_point& evaluationTime,
    RuleRuntimeDeliveryState* deliveryState) {
    std::vector<Message> emittedMessages{};
    emittedMessages.reserve(candidateMessages.size());

    const auto delaySeconds = readPositiveGateSeconds(ruleObject, "delayInSeconds");
    const auto cooldownSeconds = readPositiveGateSeconds(ruleObject, "cooldownInSeconds");
    const bool eventTriggeredRule = fieldIsArray(ruleObject, "anyOf") || fieldIsArray(ruleObject, "allOf");

    for (const auto& candidateMessage : candidateMessages) {
        const std::string outputKey = rulePath + "|" + candidateMessage.topic();
        const std::string candidateHash = deliveryComparableHash(
            candidateMessage,
            cooldownSeconds.has_value());

        auto& outputState = deliveryState->outputStatesByKey[outputKey];
        if (outputState.candidateHash != candidateHash) {
            outputState.candidateHash = candidateHash;
            outputState.candidateSince = evaluationTime;
        }

        if (delaySeconds.has_value()) {
            const auto stableFor = std::chrono::duration_cast<std::chrono::duration<double>>(
                evaluationTime - outputState.candidateSince);
            if (stableFor.count() < *delaySeconds) {
                continue;
            }
        }

        if (!outputState.emittedHash.has_value() || *outputState.emittedHash != candidateHash) {
            outputState.emittedHash = candidateHash;
            outputState.emittedAt = evaluationTime;
            emittedMessages.push_back(candidateMessage.clone());
            continue;
        }

        if (!cooldownSeconds.has_value() && eventTriggeredRule) {
            outputState.emittedAt = evaluationTime;
            emittedMessages.push_back(candidateMessage.clone());
            continue;
        }

        if (cooldownSeconds.has_value() && outputState.emittedAt.has_value()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
                evaluationTime - *outputState.emittedAt);
            if (elapsed.count() >= *cooldownSeconds) {
                outputState.emittedAt = evaluationTime;
                emittedMessages.push_back(candidateMessage.clone());
            }
        }
    }

    return emittedMessages;
}

bool shouldRetainDeliveryStateOnGateMiss(const RuleTreeNode::Object& ruleObject) {
    return readNumberField(ruleObject, "cooldownInSeconds").has_value();
}

void clearRuleDeliveryState(const std::string& rulePath, RuleRuntimeDeliveryState* deliveryState) {
    std::vector<std::string> keysToErase{};
    keysToErase.reserve(deliveryState->outputStatesByKey.size());

    const std::string keyPrefix = rulePath + "|";
    for (const auto& [stateKey, stateValue] : deliveryState->outputStatesByKey) {
        (void)stateValue;
        if (stateKey.starts_with(keyPrefix)) {
            keysToErase.push_back(stateKey);
        }
    }

    for (const auto& stateKey : keysToErase) {
        deliveryState->outputStatesByKey.erase(stateKey);
    }
}

} // namespace yaha
