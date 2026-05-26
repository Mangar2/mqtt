#include <catch2/catch_test_macros.hpp>

#include "json/json_value.h"
#include "yaha/serial_device/serial_device_contract.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::string readTextFile(const std::filesystem::path& pathValue) {
    std::ifstream stream{pathValue};
    REQUIRE(stream.good());

    std::stringstream buffer{};
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string compactJsonText(const std::string& inputText) {
    std::string outputText{};
    outputText.reserve(inputText.size());

    bool inString = false;
    bool escaping = false;
    for (const char characterValue : inputText) {
        if (inString) {
            outputText.push_back(characterValue);
            if (escaping) {
                escaping = false;
                continue;
            }
            if (characterValue == '\\') {
                escaping = true;
                continue;
            }
            if (characterValue == '"') {
                inString = false;
            }
            continue;
        }

        if (characterValue == '"') {
            inString = true;
            outputText.push_back(characterValue);
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(characterValue)) == 0) {
            outputText.push_back(characterValue);
        }
    }

    return outputText;
}

[[nodiscard]] std::filesystem::path resolveOracleFixturePath() {
    const std::vector<std::filesystem::path> candidates{
        std::filesystem::path{"test/oracle/serialdevice/data/E-subscribes/core.json"},
        std::filesystem::path{"../test/oracle/serialdevice/data/E-subscribes/core.json"},
        std::filesystem::path{"../../test/oracle/serialdevice/data/E-subscribes/core.json"},
        std::filesystem::path{__FILE__}.parent_path() / "../../../../test/oracle/serialdevice/data/E-subscribes/core.json",
        std::filesystem::path{__FILE__}.parent_path() / "../../../../../test/oracle/serialdevice/data/E-subscribes/core.json"
    };

    for (const auto& candidatePath : candidates) {
        if (std::filesystem::exists(candidatePath)) {
            return std::filesystem::canonical(candidatePath);
        }
    }

    throw std::runtime_error{
        "Unable to locate Oracle E fixture test/oracle/serialdevice/data/E-subscribes/core.json"};
}

[[nodiscard]] yaha::Qos qosFromJsonNumber(const mqtt::json::JsonValue& value) {
    REQUIRE(value.is_number());
    const auto numericValue = static_cast<std::uint32_t>(value.as_number());
    REQUIRE(numericValue <= 2U);
    return static_cast<yaha::Qos>(numericValue);
}

[[nodiscard]] std::string stringifyJsonScalar(const mqtt::json::JsonValue& value) {
    if (value.is_string()) {
        return value.as_string();
    }
    if (value.is_number()) {
        return std::to_string(static_cast<std::int64_t>(value.as_number()));
    }
    if (value.is_boolean()) {
        return value.as_boolean() ? "true" : "false";
    }
    if (value.is_null()) {
        return "null";
    }

    return value.stringify();
}

void parseCommandMap(
    const mqtt::json::JsonValue& interfaceValue,
    yaha::SerialDeviceInterfaceDefinition& definition) {
    if (!interfaceValue.contains("commandMap")) {
        return;
    }

    const auto& commandMap = interfaceValue.at("commandMap").as_object();
    for (const auto& entry : commandMap) {
        definition.commandMap[entry.first] = stringifyJsonScalar(entry.second);
    }
}

void parseReceiverMap(
    const mqtt::json::JsonValue& interfaceValue,
    yaha::SerialDeviceInterfaceDefinition& definition) {
    if (!interfaceValue.contains("receiverMap")) {
        return;
    }

    const auto& receiverMapValue = interfaceValue.at("receiverMap");
    if (receiverMapValue.is_object()) {
        definition.receiverMapProvided = true;
        for (const auto& entry : receiverMapValue.as_object()) {
            definition.receiverMap[entry.first] = stringifyJsonScalar(entry.second);
        }
        return;
    }

    if (receiverMapValue.is_null()) {
        definition.receiverMapProvided = false;
        definition.receiverMap.clear();
    }
}

void parseTopicMap(
    const mqtt::json::JsonValue& interfaceValue,
    yaha::SerialDeviceInterfaceDefinition& definition) {
    if (!interfaceValue.contains("topicMap")) {
        return;
    }

    const auto& topicMapValue = interfaceValue.at("topicMap").as_object();
    for (const auto& entry : topicMapValue) {
        yaha::SerialDeviceSwitchTopicMapping mapping{};
        if (entry.second.is_object() && entry.second.contains("command")) {
            mapping.command = stringifyJsonScalar(entry.second.at("command"));
        }
        if (entry.second.is_object() && entry.second.contains("address")) {
            mapping.address = stringifyJsonScalar(entry.second.at("address"));
        }
        if (entry.second.is_object() && entry.second.contains("value")) {
            mapping.value = static_cast<std::uint16_t>(entry.second.at("value").as_number());
        }
        definition.topicMap[entry.first] = mapping;
    }
}

[[nodiscard]] yaha::SerialDeviceInterfaceDefinition parseInterfaceDefinition(
    const mqtt::json::JsonValue& interfaceValue) {
    yaha::SerialDeviceInterfaceDefinition definition{};
    if (!interfaceValue.is_object()) {
        return definition;
    }

    parseCommandMap(interfaceValue, definition);
    parseReceiverMap(interfaceValue, definition);
    parseTopicMap(interfaceValue, definition);
    return definition;
}

[[nodiscard]] yaha::SerialDeviceConfig parseConfigFromOracleCase(const mqtt::json::JsonValue& caseValue) {
    const auto& options = caseValue.at("input").at("options");

    yaha::SerialDeviceConfig config{};
    if (options.contains("qos")) {
        config.subscribeQos = qosFromJsonNumber(options.at("qos"));
    }

    if (options.contains("interfaces")) {
        for (const auto& entry : options.at("interfaces").as_object()) {
            config.interfaces[entry.first] = parseInterfaceDefinition(entry.second);
        }
    }

    return config;
}

[[nodiscard]] yaha::SubscriptionMap parseExpectedSubscriptions(const mqtt::json::JsonValue& caseValue) {
    yaha::SubscriptionMap expected{};
    const auto& expectedObject = caseValue.at("expected").as_object();
    for (const auto& entry : expectedObject) {
        expected[entry.first] = qosFromJsonNumber(entry.second);
    }
    return expected;
}

void assertSubscriptionsEqual(
    const yaha::SubscriptionMap& actual,
    const yaha::SubscriptionMap& expected,
    const std::string& caseIdentifier) {
    INFO("case=" << caseIdentifier);
    REQUIRE(actual.size() == expected.size());
    for (const auto& entry : expected) {
        REQUIRE(actual.contains(entry.first));
        CHECK(actual.at(entry.first) == entry.second);
    }
}

} // namespace

TEST_CASE("serial_device_subscriptions_match_oracle_e_fixture", "[serial_device][parity]") {
    const auto fixturePath = resolveOracleFixturePath();
    const std::string fixtureText = readTextFile(fixturePath);
    const mqtt::json::JsonValue fixtureJson = mqtt::json::JsonValue::parse(compactJsonText(fixtureText));

    REQUIRE(fixtureJson.contains("cases"));
    const auto& cases = fixtureJson.at("cases").as_array();
    REQUIRE_FALSE(cases.empty());

    for (const auto& caseValue : cases) {
        const std::string caseIdentifier = caseValue.at("id").as_string();
        const yaha::SerialDeviceConfig config = parseConfigFromOracleCase(caseValue);
        const yaha::SubscriptionMap expected = parseExpectedSubscriptions(caseValue);
        const yaha::SubscriptionMap actual = yaha::deriveSerialDeviceSubscriptions(config);
        assertSubscriptionsEqual(actual, expected, caseIdentifier);
    }
}
