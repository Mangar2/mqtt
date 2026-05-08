#include "yaha/message_store/message_store_json_parser.h"

#include "yaha/message/message.h"
#include "yaha/message_store/iso_timestamp_parser.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace yaha::message_store_json {
namespace {

constexpr std::uint32_t k_default_level_amount{1U};

std::string trim(const std::string& value) {
    std::size_t begin = 0U;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        begin += 1U;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
        end -= 1U;
    }

    return value.substr(begin, end - begin);
}

std::string normalizeTopicPrefixForTree(const std::string& topicPrefix) {
    if (!topicPrefix.empty() && topicPrefix.front() == '/') {
        return topicPrefix.substr(1U);
    }
    return topicPrefix;
}

std::string toLower(std::string value) {
    for (char& charValue : value) {
        charValue = static_cast<char>(
            std::tolower(static_cast<unsigned char>(charValue)));
    }
    return value;
}

bool tryParseLegacyBoolToken(const std::string& tokenRaw, bool& output) {
    const std::string token = toLower(trim(tokenRaw));
    if (token == "1" || token == "true" || token == "yes" || token == "on") {
        output = true;
        return true;
    }
    if (token == "0" || token == "false" || token == "no" || token == "off") {
        output = false;
        return true;
    }
    return false;
}

bool tryParseUnsignedValue(const std::string& text,
                           std::uint32_t defaultValue,
                           std::uint32_t& output) {
    const std::string cleaned = trim(text);
    if (cleaned.empty()) {
        output = defaultValue;
        return true;
    }

    char* endPtr = nullptr;
    const unsigned long parsed = std::strtoul(cleaned.c_str(), &endPtr, 10);
    if (endPtr == nullptr || *endPtr != '\0') {
        output = defaultValue;
        return false;
    }

    output = static_cast<std::uint32_t>(parsed);
    return true;
}

class SnapshotJsonParser {
public:
    explicit SnapshotJsonParser(std::string json)
        : json_(std::move(json)) {}

    bool parse(std::vector<MessageTreeSnapshotNode>& out) {
        skipWs();
        if (!consume('[')) {
            return false;
        }
        skipWs();
        if (consume(']')) {
            return true;
        }

        while (true) {
            MessageTreeSnapshotNode node{};
            if (!parseObject(node)) {
                return false;
            }
            out.push_back(std::move(node));
            skipWs();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

private:
    bool parseObject(MessageTreeSnapshotNode& node) {
        if (!consume('{')) {
            return false;
        }

        bool hasTopic = false;
        bool hasValue = false;
        skipWs();
        if (consume('}')) {
            return false;
        }

        while (true) {
            std::string key;
            if (!parseString(key)) {
                return false;
            }
            if (!consume(':')) {
                return false;
            }

            if (!parseObjectField(key, node, hasTopic, hasValue)) {
                return false;
            }

            skipWs();
            if (consume('}')) {
                return hasTopic && hasValue;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

    bool parseObjectField(const std::string& key,
                          MessageTreeSnapshotNode& node,
                          bool& hasTopic,
                          bool& hasValue) {
        if (key == "topic") {
            if (!parseString(node.topic)) {
                return false;
            }
            hasTopic = true;
            return true;
        }

        if (key == "value") {
            Value parsed{};
            if (!parseValue(parsed)) {
                return false;
            }
            node.value = std::move(parsed);
            hasValue = true;
            return true;
        }

        if (key == "time") {
            return parseSnapshotTime(node.timeMs);
        }

        return skipValue();
    }

    bool parseValue(Value& output) {
        skipWs();
        std::string text{};
        if (peek() == '"') {
            if (!parseString(text)) {
                return false;
            }
            output = std::move(text);
            return true;
        }

        double number = 0.0;
        if (!parseNumber(number)) {
            return false;
        }
        output = number;
        return true;
    }

    bool parseSnapshotTime(std::optional<std::int64_t>& output) {
        skipWs();
        if (peek() == '"') {
            std::string parsedTimestamp{};
            if (!parseString(parsedTimestamp)) {
                return false;
            }

            std::int64_t parsedTimeMs = 0;
            if (tryParseIsoTimestampMilliseconds(parsedTimestamp, parsedTimeMs)) {
                output = parsedTimeMs;
            }
            return true;
        }

        double parsedNumber = 0.0;
        if (parseNumber(parsedNumber)) {
            if (std::isfinite(parsedNumber) && std::floor(parsedNumber) == parsedNumber) {
                output = static_cast<std::int64_t>(parsedNumber);
            }
            return true;
        }

        return skipValue();
    }

    bool skipValue() {
        skipWs();
        const char current = peek();
        if (current == '"') {
            std::string ignored;
            return parseString(ignored);
        }
        if (current == '{') {
            return skipNestedStructure('{', '}');
        }
        if (current == '[') {
            return skipNestedStructure('[', ']');
        }

        return skipPrimitiveValue();
    }

    bool skipNestedStructure(char openChar, char closeChar) {
        int depth = 0;
        do {
            const char currentChar = peek();
            if (currentChar == '\0') {
                return false;
            }
            if (currentChar == openChar) {
                depth += 1;
            }
            if (currentChar == closeChar) {
                depth -= 1;
            }
            pos_ += 1U;
        } while (depth > 0);
        return true;
    }

    bool skipPrimitiveValue() {
        double ignored = 0.0;
        if (parseNumber(ignored)) {
            return true;
        }
        return parseLiteral("true") || parseLiteral("false") || parseLiteral("null");
    }

    bool parseNumber(double& out) {
        skipWs();
        std::size_t consumed = 0U;
        try {
            out = std::stod(json_.substr(pos_), &consumed);
        } catch (...) {
            return false;
        }
        pos_ += consumed;
        return consumed > 0U;
    }

    bool parseString(std::string& out) {
        skipWs();
        if (!consume('"')) {
            return false;
        }

        std::string result;
        while (pos_ < json_.size()) {
            const char currentChar = json_[pos_++];
            if (currentChar == '"') {
                out = std::move(result);
                return true;
            }
            if (currentChar == '\\') {
                if (pos_ >= json_.size()) {
                    return false;
                }
                const char escaped = json_[pos_++];
                switch (escaped) {
                    case '"':
                    case '\\':
                    case '/':
                        result.push_back(escaped);
                        break;
                    case 'n':
                        result.push_back('\n');
                        break;
                    case 'r':
                        result.push_back('\r');
                        break;
                    case 't':
                        result.push_back('\t');
                        break;
                    default:
                        return false;
                }
                continue;
            }
            result.push_back(currentChar);
        }

        return false;
    }

    bool parseLiteral(const std::string& literal) {
        skipWs();
        if (json_.substr(pos_, literal.size()) != literal) {
            return false;
        }
        pos_ += literal.size();
        return true;
    }

    bool consume(char expected) {
        skipWs();
        if (peek() != expected) {
            return false;
        }
        pos_ += 1U;
        return true;
    }

    char peek() const {
        if (pos_ >= json_.size()) {
            return '\0';
        }
        return json_[pos_];
    }

    void skipWs() {
        while (pos_ < json_.size() && std::isspace(static_cast<unsigned char>(json_[pos_])) != 0) {
            pos_ += 1U;
        }
    }

    std::string json_;
    std::size_t pos_{0U};
};

class SensorPostJsonParser {
public:
    explicit SensorPostJsonParser(std::string json)
        : json_(std::move(json)) {}

    bool parse(SensorPostRequest& output) {
        skipWs();
        if (!consume('{')) {
            return false;
        }

        skipWs();
        if (consume('}')) {
            return true;
        }

        while (true) {
            std::string key{};
            if (!parseString(key)) {
                return false;
            }

            if (!consume(':')) {
                return false;
            }

            if (!parseField(key, output)) {
                return false;
            }

            skipWs();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

private:
    bool parseField(const std::string& key, SensorPostRequest& output) {
        if (key == "topic") {
            std::string topic{};
            if (!parseString(topic)) {
                return false;
            }
            output.topicPrefix = normalizeTopicPrefixForTree(topic);
            return true;
        }

        if (key == "history") {
            std::string historyRaw{};
            if (!captureRawValue(historyRaw)) {
                return false;
            }
            bool includeHistory = false;
            if (tryParseBoolValue(historyRaw, includeHistory)) {
                output.includeHistory = includeHistory;
            } else {
                output.includeHistory = false;
            }
            return true;
        }

        if (key == "reason") {
            std::string reasonRaw{};
            if (!captureRawValue(reasonRaw)) {
                return false;
            }
            bool includeReason = false;
            if (tryParseBoolValue(reasonRaw, includeReason)) {
                output.includeReason = includeReason;
            } else {
                output.includeReason = false;
            }
            return true;
        }

        if (key == "levelAmount" || key == "levelamount") {
            std::string levelAmountRaw{};
            if (!captureRawValue(levelAmountRaw)) {
                return false;
            }
            applyLevelAmount(levelAmountRaw, output.levelAmount);
            return true;
        }

        if (key == "nodes") {
            std::string nodesRaw{};
            if (!captureRawValue(nodesRaw)) {
                return false;
            }
            const std::string trimmedNodesRaw = trim(nodesRaw);
            output.hasNodes = !trimmedNodesRaw.empty() && trimmedNodesRaw != "[]" && trimmedNodesRaw != "null";
            output.nodesJson = output.hasNodes ? trimmedNodesRaw : std::string{};
            return true;
        }

        return skipValue();
    }

    static void applyLevelAmount(const std::string& rawValue, std::uint32_t& output) {
        const std::string cleaned = trim(rawValue);
        if (cleaned.empty()) {
            output = k_default_level_amount;
            return;
        }

        if (cleaned.front() == '"') {
            std::string textValue{};
            SensorPostJsonParser decoder{cleaned};
            if (!decoder.parseSingleString(textValue)) {
                output = k_default_level_amount;
                return;
            }
            (void)tryParseUnsignedValue(textValue, k_default_level_amount, output);
            return;
        }

        double numericValue = 0.0;
        std::size_t consumed = 0U;
        try {
            numericValue = std::stod(cleaned, &consumed);
        } catch (...) {
            output = k_default_level_amount;
            return;
        }

        if (consumed != cleaned.size() || !std::isfinite(numericValue) || numericValue < 0.0
            || std::floor(numericValue) != numericValue) {
            output = k_default_level_amount;
            return;
        }

        output = static_cast<std::uint32_t>(numericValue);
    }

    static bool tryParseBoolValue(const std::string& rawValue, bool& output) {
        const std::string cleaned = trim(rawValue);
        if (cleaned.empty()) {
            return false;
        }

        if (cleaned.front() == '"') {
            std::string textValue{};
            SensorPostJsonParser decoder{cleaned};
            if (!decoder.parseSingleString(textValue)) {
                return false;
            }
            return tryParseLegacyBoolToken(textValue, output);
        }

        return tryParseLegacyBoolToken(cleaned, output);
    }

    bool parseSingleString(std::string& output) {
        skipWs();
        if (!parseString(output)) {
            return false;
        }
        skipWs();
        return pos_ == json_.size();
    }

    bool captureRawValue(std::string& output) {
        skipWs();
        const std::size_t start = pos_;
        if (!skipValue()) {
            return false;
        }
        output = json_.substr(start, pos_ - start);
        return true;
    }

    bool skipValue() {
        skipWs();
        const char currentChar = peek();
        if (currentChar == '"') {
            std::string ignored{};
            return parseString(ignored);
        }
        if (currentChar == '{' || currentChar == '[') {
            return skipNestedJson();
        }

        double ignoredNumber = 0.0;
        if (parseNumber(ignoredNumber)) {
            return true;
        }

        return parseLiteral("true") || parseLiteral("false") || parseLiteral("null");
    }

    bool skipNestedJson() {
        std::vector<char> expectedClosings{};
        if (!initializeNestedJson(expectedClosings)) {
            return false;
        }

        bool inString = false;
        bool escaped = false;
        while (!expectedClosings.empty()) {
            char currentChar = '\0';
            if (!readNextChar(currentChar)) {
                return false;
            }

            if (inString) {
                updateStringState(currentChar, inString, escaped);
                continue;
            }

            if (currentChar == '"') {
                inString = true;
                continue;
            }

            if (!updateNestedState(currentChar, expectedClosings)) {
                return false;
            }
        }

        return true;
    }

    bool initializeNestedJson(std::vector<char>& expectedClosings) {
        const char firstChar = peek();
        if (firstChar == '{') {
            expectedClosings.push_back('}');
            pos_ += 1U;
            return true;
        }
        if (firstChar == '[') {
            expectedClosings.push_back(']');
            pos_ += 1U;
            return true;
        }
        return false;
    }

    bool readNextChar(char& output) {
        if (pos_ >= json_.size()) {
            return false;
        }
        output = json_[pos_++];
        return true;
    }

    static void updateStringState(char currentChar, bool& inString, bool& escaped) {
        if (escaped) {
            escaped = false;
            return;
        }
        if (currentChar == '\\') {
            escaped = true;
            return;
        }
        if (currentChar == '"') {
            inString = false;
        }
    }

    static bool isClosingChar(char currentChar) {
        return currentChar == '}' || currentChar == ']';
    }

    static bool updateNestedState(char currentChar, std::vector<char>& expectedClosings) {
        if (currentChar == '{') {
            expectedClosings.push_back('}');
            return true;
        }
        if (currentChar == '[') {
            expectedClosings.push_back(']');
            return true;
        }
        if (!isClosingChar(currentChar)) {
            return true;
        }
        if (expectedClosings.back() != currentChar) {
            return false;
        }
        expectedClosings.pop_back();
        return true;
    }

    bool parseNumber(double& output) {
        skipWs();
        std::size_t consumed = 0U;
        try {
            output = std::stod(json_.substr(pos_), &consumed);
        } catch (...) {
            return false;
        }
        if (consumed == 0U) {
            return false;
        }
        pos_ += consumed;
        return true;
    }

    bool parseString(std::string& output) {
        skipWs();
        if (!consume('"')) {
            return false;
        }

        std::string parsed{};
        while (pos_ < json_.size()) {
            const char currentChar = json_[pos_++];
            if (currentChar == '"') {
                output = std::move(parsed);
                return true;
            }
            if (currentChar == '\\') {
                if (pos_ >= json_.size()) {
                    return false;
                }
                const char escaped = json_[pos_++];
                switch (escaped) {
                    case '"':
                    case '\\':
                    case '/':
                        parsed.push_back(escaped);
                        break;
                    case 'n':
                        parsed.push_back('\n');
                        break;
                    case 'r':
                        parsed.push_back('\r');
                        break;
                    case 't':
                        parsed.push_back('\t');
                        break;
                    default:
                        return false;
                }
                continue;
            }
            parsed.push_back(currentChar);
        }

        return false;
    }

    bool parseLiteral(const std::string& literal) {
        skipWs();
        if (json_.substr(pos_, literal.size()) != literal) {
            return false;
        }
        pos_ += literal.size();
        return true;
    }

    bool consume(char expected) {
        skipWs();
        if (peek() != expected) {
            return false;
        }
        pos_ += 1U;
        return true;
    }

    char peek() const {
        if (pos_ >= json_.size()) {
            return '\0';
        }
        return json_[pos_];
    }

    void skipWs() {
        while (pos_ < json_.size() && std::isspace(static_cast<unsigned char>(json_[pos_])) != 0) {
            pos_ += 1U;
        }
    }

    std::string json_;
    std::size_t pos_{0U};
};

} // namespace

bool parseSnapshotBody(const std::string& body, std::vector<MessageTreeSnapshotNode>& out) {
    SnapshotJsonParser parser{body};
    return parser.parse(out);
}

bool parseSensorPostBody(const std::string& body, SensorPostRequest& output) {
    SensorPostJsonParser parser{body};
    return parser.parse(output);
}

} // namespace yaha::message_store_json
