#include <catch2/catch_test_macros.hpp>

#include "json/json_value.h"
#include "yaha/serial_device/serial_device_parser.h"

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

[[nodiscard]] std::filesystem::path resolveOracleRootPath() {
    const std::vector<std::filesystem::path> candidates{
        std::filesystem::path{"test/oracle/serialdevice/data/A-parser"},
        std::filesystem::path{"../test/oracle/serialdevice/data/A-parser"},
        std::filesystem::path{"../../test/oracle/serialdevice/data/A-parser"},
        std::filesystem::path{__FILE__}.parent_path() / "../../../../test/oracle/serialdevice/data/A-parser",
        std::filesystem::path{__FILE__}.parent_path() / "../../../../../test/oracle/serialdevice/data/A-parser"
    };

    for (const auto& candidatePath : candidates) {
        if (std::filesystem::exists(candidatePath)) {
            return std::filesystem::canonical(candidatePath);
        }
    }

    throw std::runtime_error{"Unable to locate Oracle A parser fixtures"};
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

[[nodiscard]] yaha::SerialDeviceMessage messageFromOracleJson(const mqtt::json::JsonValue& messageValue) {
    yaha::SerialDeviceMessage message{};
    message.interfaceName = messageValue.at("interfaceName").as_string();
    message.sender = endpointFromJson(messageValue.at("sender"));
    message.receiver = endpointFromJson(messageValue.at("receiver"));
    message.command = messageValue.at("command").as_string();
    message.value = payloadFromJson(messageValue.at("value"));
    message.action = messageValue.at("action").as_string();
    return message;
}

void collectMessagesFromChunk(
    yaha::SerialDeviceStreamParser& parser,
    const std::string& chunkText,
    std::vector<yaha::SerialDeviceMessage>& outputMessages) {
    if (const auto firstMessage = parser.parseChunk(chunkText); firstMessage.has_value()) {
        outputMessages.push_back(firstMessage.value());
    }

    while (true) {
        const auto nextMessage = parser.parseChunk("");
        if (!nextMessage.has_value()) {
            break;
        }
        outputMessages.push_back(nextMessage.value());
    }
}

void assertSuiteMatchesOracle(const std::filesystem::path& fixturePath) {
    const std::string fixtureText = readTextFile(fixturePath);
    const mqtt::json::JsonValue fixtureJson = mqtt::json::JsonValue::parse(compactJsonText(fixtureText));

    REQUIRE(fixtureJson.contains("cases"));
    for (const auto& caseValue : fixtureJson.at("cases").as_array()) {
        const std::string caseIdentifier = caseValue.at("id").as_string();
        INFO("case=" << caseIdentifier);

        yaha::SerialDeviceStreamParser parser{};
        std::vector<yaha::SerialDeviceMessage> actualMessages{};

        for (const auto& chunkValue : caseValue.at("input").at("chunks").as_array()) {
            collectMessagesFromChunk(parser, chunkValue.as_string(), actualMessages);
        }
        collectMessagesFromChunk(parser, "", actualMessages);

        std::vector<yaha::SerialDeviceMessage> expectedMessages{};
        for (const auto& messageValue : caseValue.at("expected").as_array()) {
            expectedMessages.push_back(messageFromOracleJson(messageValue));
        }

        REQUIRE(actualMessages.size() == expectedMessages.size());
        for (std::size_t indexValue = 0; indexValue < expectedMessages.size(); ++indexValue) {
            CHECK(actualMessages.at(indexValue) == expectedMessages.at(indexValue));
        }
    }
}

} // namespace

TEST_CASE("serial_device_parser_matches_oracle_a_fixtures", "[serial_device][parity]") {
    const std::filesystem::path parserRoot = resolveOracleRootPath();
    assertSuiteMatchesOracle(parserRoot / "core.json");
    assertSuiteMatchesOracle(parserRoot / "malformed.json");
}
