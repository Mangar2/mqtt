#include <catch2/catch_test_macros.hpp>

#include "json/json_value.h"
#include "yaha/serial_device/serial_device_wire_serializer.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
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
        std::filesystem::path{"test/oracle/serialdevice/data/D-wire-serialization/core.json"},
        std::filesystem::path{"../test/oracle/serialdevice/data/D-wire-serialization/core.json"},
        std::filesystem::path{"../../test/oracle/serialdevice/data/D-wire-serialization/core.json"},
        std::filesystem::path{__FILE__}.parent_path() / "../../../../test/oracle/serialdevice/data/D-wire-serialization/core.json",
        std::filesystem::path{__FILE__}.parent_path() / "../../../../../test/oracle/serialdevice/data/D-wire-serialization/core.json"
    };

    for (const auto& candidatePath : candidates) {
        if (std::filesystem::exists(candidatePath)) {
            return std::filesystem::canonical(candidatePath);
        }
    }

    throw std::runtime_error{
        "Unable to locate Oracle D fixture test/oracle/serialdevice/data/D-wire-serialization/core.json"};
}

[[nodiscard]] yaha::SerialDeviceEndpoint endpointFromJson(const mqtt::json::JsonValue& value) {
    if (value.is_null()) {
        return yaha::SerialDeviceEndpoint{};
    }
    if (value.is_number()) {
        return yaha::SerialDeviceEndpoint{static_cast<std::int64_t>(value.as_number())};
    }
    return yaha::SerialDeviceEndpoint{value.as_string()};
}

[[nodiscard]] yaha::SerialDeviceValue payloadFromJson(const mqtt::json::JsonValue& value) {
    if (value.is_number()) {
        return yaha::SerialDeviceValue{static_cast<std::int64_t>(value.as_number())};
    }
    return yaha::SerialDeviceValue{value.as_string()};
}

[[nodiscard]] yaha::SerialDeviceMessage messageFromOracleInput(const mqtt::json::JsonValue& inputValue) {
    yaha::SerialDeviceMessage message{};
    message.interfaceName = inputValue.at("interfaceName").as_string();
    message.sender = endpointFromJson(inputValue.at("sender"));
    message.receiver = endpointFromJson(inputValue.at("receiver"));
    message.command = inputValue.at("command").as_string();
    message.value = payloadFromJson(inputValue.at("value"));
    message.action = "";
    return message;
}

} // namespace

TEST_CASE("serial_device_wire_serialization_matches_oracle_d_fixture", "[serial_device][parity]") {
    const std::filesystem::path fixturePath = resolveOracleFixturePath();
    const std::string fixtureText = readTextFile(fixturePath);
    const mqtt::json::JsonValue fixtureJson = mqtt::json::JsonValue::parse(compactJsonText(fixtureText));

    REQUIRE(fixtureJson.contains("cases"));
    const auto& cases = fixtureJson.at("cases").as_array();
    REQUIRE_FALSE(cases.empty());

    for (const auto& caseValue : cases) {
        const std::string caseIdentifier = caseValue.at("id").as_string();
        INFO("case=" << caseIdentifier);

        const yaha::SerialDeviceMessage message = messageFromOracleInput(caseValue.at("input"));
        const std::string expectedPayload = caseValue.at("expected").as_string();
        const std::string actualPayload = yaha::serialDeviceMessageToWireString(message);

        CHECK(actualPayload == expectedPayload);
    }
}
