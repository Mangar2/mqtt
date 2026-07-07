#include "yaha/message_store/message_store_json_parser.h"

#include "json/json_value.h"
#include "yaha/message_store/iso_timestamp_parser.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace yaha::message_store_json {
namespace {

constexpr std::uint32_t k_default_level_amount{1U};

std::string trim(const std::string& valueText) {
    std::size_t beginIndex = 0U;
    while (beginIndex < valueText.size() && std::isspace(static_cast<unsigned char>(valueText[beginIndex])) != 0) {
        beginIndex += 1U;
    }

    std::size_t endIndex = valueText.size();
    while (endIndex > beginIndex && std::isspace(static_cast<unsigned char>(valueText[endIndex - 1U])) != 0) {
        endIndex -= 1U;
    }

    return valueText.substr(beginIndex, endIndex - beginIndex);
}

std::string normalizeTopicPrefixForTree(const std::string& topicPrefix) {
    if (!topicPrefix.empty() && topicPrefix.front() == '/') {
        return topicPrefix.substr(1U);
    }
    return topicPrefix;
}

std::string toLower(std::string valueText) {
    for (char& charValue : valueText) {
        charValue = static_cast<char>(std::tolower(static_cast<unsigned char>(charValue)));
    }
    return valueText;
}

bool tryParseLegacyBoolToken(const std::string& tokenRaw, bool& outputValue) {
    const std::string tokenText = toLower(trim(tokenRaw));
    if (tokenText == "1" || tokenText == "true" || tokenText == "yes" || tokenText == "on") {
        outputValue = true;
        return true;
    }
    if (tokenText == "0" || tokenText == "false" || tokenText == "no" || tokenText == "off") {
        outputValue = false;
        return true;
    }
    return false;
}

bool tryParseUnsignedValue(const std::string& valueText,
                           const std::uint32_t defaultValue,
                           std::uint32_t& outputValue) {
    const std::string cleanedText = trim(valueText);
    if (cleanedText.empty()) {
        outputValue = defaultValue;
        return true;
    }

    char* parseEnd = nullptr;
    const unsigned long parsedNumber = std::strtoul(cleanedText.c_str(), &parseEnd, 10);
    if (parseEnd == nullptr || *parseEnd != '\0') {
        outputValue = defaultValue;
        return false;
    }

    outputValue = static_cast<std::uint32_t>(parsedNumber);
    return true;
}

bool parseSnapshotReasonArray(const mqtt::json::JsonValue::Array& reasonArray,
                              ReasonList& reasonOutput) {
    reasonOutput.clear();
    reasonOutput.reserve(reasonArray.size());
    for (const auto& reasonNode : reasonArray) {
        if (!reasonNode.is_object() || !reasonNode.contains("message") || !reasonNode.contains("timestamp")) {
            return false;
        }

        const mqtt::json::JsonValue& messageNode = reasonNode.at("message");
        const mqtt::json::JsonValue& timestampNode = reasonNode.at("timestamp");
        if (!messageNode.is_string() || !timestampNode.is_string()) {
            return false;
        }

        reasonOutput.push_back(ReasonEntry{
            .message = messageNode.as_string(),
            .timestamp = timestampNode.as_string(),
        });
    }

    return true;
}

bool parseSnapshotNode(const mqtt::json::JsonValue& snapshotNodeValue,
                       MessageSnapshot& snapshotNodeOutput) {
    if (!snapshotNodeValue.is_object()) {
        return false;
    }

    if (!snapshotNodeValue.contains("topic") || !snapshotNodeValue.contains("value")) {
        return false;
    }

    const mqtt::json::JsonValue& topicNode = snapshotNodeValue.at("topic");
    const mqtt::json::JsonValue& valueNode = snapshotNodeValue.at("value");
    if (!topicNode.is_string()) {
        return false;
    }

    if (valueNode.is_string()) {
        snapshotNodeOutput.value = valueNode.as_string();
    } else if (valueNode.is_number()) {
        snapshotNodeOutput.value = valueNode.as_number();
    } else {
        return false;
    }

    snapshotNodeOutput.topic = topicNode.as_string();

    if (snapshotNodeValue.contains("time")) {
        const mqtt::json::JsonValue& timeNode = snapshotNodeValue.at("time");
        if (timeNode.is_string()) {
            std::int64_t parsedTimeMilliseconds = 0;
            if (tryParseIsoTimestampMilliseconds(timeNode.as_string(), parsedTimeMilliseconds)) {
                snapshotNodeOutput.timeMs = parsedTimeMilliseconds;
            }
        } else if (timeNode.is_number()) {
            const double numericTime = timeNode.as_number();
            if (std::isfinite(numericTime) && std::floor(numericTime) == numericTime) {
                snapshotNodeOutput.timeMs = static_cast<std::int64_t>(numericTime);
            }
        }
    }

    if (snapshotNodeValue.contains("reason")) {
        const mqtt::json::JsonValue& reasonNode = snapshotNodeValue.at("reason");
        if (!reasonNode.is_array()) {
            return false;
        }

        if (!parseSnapshotReasonArray(reasonNode.as_array(), snapshotNodeOutput.reason)) {
            return false;
        }
    }

    return true;
}

void applySensorBooleanField(const mqtt::json::JsonValue& fieldValue,
                             bool& fieldOutput) {
    if (fieldValue.is_boolean()) {
        fieldOutput = fieldValue.as_boolean();
        return;
    }

    if (fieldValue.is_string()) {
        bool parsedValue = false;
        if (tryParseLegacyBoolToken(fieldValue.as_string(), parsedValue)) {
            fieldOutput = parsedValue;
            return;
        }
    }

    fieldOutput = false;
}

void applySensorLevelAmountField(const mqtt::json::JsonValue& fieldValue,
                                 std::uint32_t& levelAmountOutput) {
    if (fieldValue.is_string()) {
        (void)tryParseUnsignedValue(fieldValue.as_string(), k_default_level_amount, levelAmountOutput);
        return;
    }

    if (fieldValue.is_number()) {
        const double numericValue = fieldValue.as_number();
        if (std::isfinite(numericValue)
            && numericValue >= 0.0
            && std::floor(numericValue) == numericValue
            && numericValue <= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
            levelAmountOutput = static_cast<std::uint32_t>(numericValue);
            return;
        }
    }

    levelAmountOutput = k_default_level_amount;
}

} // namespace

bool parseSnapshotBody(const std::string& body, std::vector<MessageSnapshot>& out) {
    const auto parsedRoot = mqtt::json::JsonValue::try_parse(body);
    if (!parsedRoot.has_value() || !parsedRoot->is_array()) {
        return false;
    }

    std::vector<MessageSnapshot> parsedNodes{};
    parsedNodes.reserve(parsedRoot->as_array().size());
    for (const auto& snapshotNodeValue : parsedRoot->as_array()) {
        MessageSnapshot parsedNode{};
        if (!parseSnapshotNode(snapshotNodeValue, parsedNode)) {
            return false;
        }
        parsedNodes.push_back(std::move(parsedNode));
    }

    out = std::move(parsedNodes);
    return true;
}

bool parseSensorPostBody(const std::string& body, SensorPostRequest& output) {
    const auto parsedRoot = mqtt::json::JsonValue::try_parse(body);
    if (!parsedRoot.has_value() || !parsedRoot->is_object()) {
        return false;
    }

    const mqtt::json::JsonValue::Object& rootObject = parsedRoot->as_object();

    if (rootObject.contains("topic")) {
        const mqtt::json::JsonValue& topicValue = rootObject.at("topic");
        if (!topicValue.is_string()) {
            return false;
        }
        output.topicPrefix = normalizeTopicPrefixForTree(topicValue.as_string());
    }

    if (rootObject.contains("history")) {
        applySensorBooleanField(rootObject.at("history"), output.includeHistory);
    }

    if (rootObject.contains("reason")) {
        applySensorBooleanField(rootObject.at("reason"), output.includeReason);
    }

    if (rootObject.contains("time")) {
        applySensorBooleanField(rootObject.at("time"), output.includeTime);
    }

    if (rootObject.contains("levelAmount")) {
        output.hasLevelAmount = true;
        applySensorLevelAmountField(rootObject.at("levelAmount"), output.levelAmount);
    } else if (rootObject.contains("levelamount")) {
        output.hasLevelAmount = true;
        applySensorLevelAmountField(rootObject.at("levelamount"), output.levelAmount);
    }

    if (rootObject.contains("nodes")) {
        const std::string nodesText = trim(rootObject.at("nodes").stringify());
        output.hasNodes = !nodesText.empty() && nodesText != "[]" && nodesText != "null";
        output.nodesJson = output.hasNodes ? nodesText : std::string{};
    }

    return true;
}

} // namespace yaha::message_store_json
