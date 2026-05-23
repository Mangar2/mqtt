#include "json/json_value.h"

#include "json/json_error.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace mqtt::json {

JsonException::JsonException(JsonError errorCode, const std::string& messageText, std::size_t offsetValue)
    : std::runtime_error(messageText),
      errorCode_(errorCode),
      offsetValue_(offsetValue) {}

JsonError JsonException::error() const noexcept {
    return errorCode_;
}

std::size_t JsonException::offset() const noexcept {
    return offsetValue_;
}

namespace {

constexpr std::uint32_t k_hex_base{16U};
constexpr std::uint32_t k_decimal_ten{10U};
constexpr std::uint32_t k_json_control_boundary{0x20U};
constexpr std::uint32_t k_high_surrogate_min{0xD800U};
constexpr std::uint32_t k_high_surrogate_max{0xDBFFU};
constexpr std::uint32_t k_low_surrogate_min{0xDC00U};
constexpr std::uint32_t k_low_surrogate_max{0xDFFFU};
constexpr std::uint32_t k_supplementary_offset{0x10000U};
constexpr std::uint32_t k_surrogate_base{0x400U};
constexpr std::uint32_t k_hex_nibble_mask{0x0FU};
constexpr std::uint32_t k_high_nibble_shift{4U};
constexpr std::size_t k_number_precision_digits{15U};
constexpr std::uint32_t k_utf8_one_byte_max{0x7FU};
constexpr std::uint32_t k_utf8_two_byte_max{0x7FFU};
constexpr std::uint32_t k_utf8_three_byte_max{0xFFFFU};
constexpr std::uint32_t k_utf8_continuation_mask{0x3FU};
constexpr std::uint32_t k_utf8_two_byte_prefix{0xC0U};
constexpr std::uint32_t k_utf8_three_byte_prefix{0xE0U};
constexpr std::uint32_t k_utf8_four_byte_prefix{0xF0U};
constexpr std::uint32_t k_utf8_continuation_prefix{0x80U};
constexpr std::uint32_t k_utf8_two_byte_head_mask{0x1FU};
constexpr std::uint32_t k_utf8_three_byte_head_mask{0x0FU};
constexpr std::uint32_t k_utf8_four_byte_head_mask{0x07U};
constexpr std::uint32_t k_utf8_shift_six{6U};
constexpr std::uint32_t k_utf8_shift_twelve{12U};
constexpr std::uint32_t k_utf8_shift_eighteen{18U};

[[noreturn]] void throw_json_error(JsonError errorCode,
                                   std::size_t offsetValue,
                                   std::string_view messageText) {
    throw JsonException(errorCode,
                        std::format("{} at offset {}", messageText, offsetValue),
                        offsetValue);
}

class JsonParser final {
public:
    explicit JsonParser(std::string_view sourceText)
        : sourceText_(sourceText) {}

    [[nodiscard]] JsonValue parse_document() {
        skip_whitespace();
        const auto parsedValue = parse_value();
        skip_whitespace();
        if (!is_end()) {
            throw_json_error(JsonError::UnexpectedToken, offsetValue_, "unexpected trailing token");
        }
        return parsedValue;
    }

private:
    [[nodiscard]] JsonValue parse_value() {
        skip_whitespace();
        if (is_end()) {
            throw_json_error(JsonError::UnexpectedEndOfInput, offsetValue_, "unexpected end of input");
        }

        const auto nextChar = peek_char();
        if (nextChar == '{') {
            return parse_object();
        }
        if (nextChar == '[') {
            return parse_array();
        }
        if (nextChar == '"') {
            return JsonValue{parse_string()};
        }
        if (nextChar == 't') {
            consume_keyword("true");
            return JsonValue{true};
        }
        if (nextChar == 'f') {
            consume_keyword("false");
            return JsonValue{false};
        }
        if (nextChar == 'n') {
            consume_keyword("null");
            return JsonValue{};
        }
        if (nextChar == '-' || std::isdigit(static_cast<unsigned char>(nextChar)) != 0) {
            return JsonValue{parse_number()};
        }

        throw_json_error(JsonError::UnexpectedToken, offsetValue_, "unexpected token");
    }

    [[nodiscard]] JsonValue parse_object() {
        expect_char('{');

        JsonValue::Object objectValue{};
        skip_whitespace();
        if (consume_if('}')) {
            return JsonValue{std::move(objectValue)};
        }

        while (true) {
            if (peek_char() != '"') {
                throw_json_error(JsonError::UnexpectedToken, offsetValue_, "expected object key string");
            }
            auto keyName = parse_string();
            skip_whitespace();
            expect_char(':');
            objectValue[std::move(keyName)] = parse_value();

            skip_whitespace();
            if (consume_if('}')) {
                break;
            }
            expect_char(',');
        }

        return JsonValue{std::move(objectValue)};
    }

    [[nodiscard]] JsonValue parse_array() {
        expect_char('[');

        JsonValue::Array arrayValue{};
        skip_whitespace();
        if (consume_if(']')) {
            return JsonValue{std::move(arrayValue)};
        }

        while (true) {
            arrayValue.push_back(parse_value());
            skip_whitespace();
            if (consume_if(']')) {
                break;
            }
            expect_char(',');
        }

        return JsonValue{std::move(arrayValue)};
    }

    [[nodiscard]] std::string parse_string() {
        expect_char('"');
        std::string resultText{};

        while (!is_end()) {
            const auto currentChar = read_char();
            if (currentChar == '"') {
                return resultText;
            }

            if (static_cast<unsigned char>(currentChar) < k_json_control_boundary) {
                throw_json_error(JsonError::UnexpectedToken, offsetValue_, "control character in string");
            }

            if (currentChar != '\\') {
                resultText.push_back(currentChar);
                continue;
            }

            if (is_end()) {
                throw_json_error(JsonError::UnexpectedEndOfInput, offsetValue_, "unterminated string escape");
            }

            const auto escapeCode = read_char();
            switch (escapeCode) {
                case '"':
                    resultText.push_back('"');
                    break;
                case '\\':
                    resultText.push_back('\\');
                    break;
                case '/':
                    resultText.push_back('/');
                    break;
                case 'b':
                    resultText.push_back('\b');
                    break;
                case 'f':
                    resultText.push_back('\f');
                    break;
                case 'n':
                    resultText.push_back('\n');
                    break;
                case 'r':
                    resultText.push_back('\r');
                    break;
                case 't':
                    resultText.push_back('\t');
                    break;
                case 'u': {
                    const auto codepointValue = parse_unicode_codepoint();
                    append_utf8(codepointValue, resultText);
                    break;
                }
                default:
                    throw_json_error(JsonError::InvalidStringEscape, offsetValue_, "invalid string escape");
            }
        }

        throw_json_error(JsonError::UnexpectedEndOfInput, offsetValue_, "unterminated string literal");
    }

    [[nodiscard]] std::uint32_t parse_unicode_codepoint() {
        const auto firstCodeUnit = parse_hex_code_unit();

        if (firstCodeUnit >= k_high_surrogate_min && firstCodeUnit <= k_high_surrogate_max) {
            if (!consume_if('\\')) {
                throw_json_error(JsonError::InvalidUnicodeEscape,
                                 offsetValue_,
                                 "missing low surrogate after high surrogate");
            }
            if (!consume_if('u')) {
                throw_json_error(JsonError::InvalidUnicodeEscape,
                                 offsetValue_,
                                 "missing low surrogate marker");
            }
            const auto secondCodeUnit = parse_hex_code_unit();
            if (secondCodeUnit < k_low_surrogate_min || secondCodeUnit > k_low_surrogate_max) {
                throw_json_error(JsonError::InvalidUnicodeEscape,
                                 offsetValue_,
                                 "invalid low surrogate code unit");
            }

            const auto highValue = firstCodeUnit - k_high_surrogate_min;
            const auto lowValue = secondCodeUnit - k_low_surrogate_min;
            return k_supplementary_offset + (highValue * k_surrogate_base) + lowValue;
        }

        if (firstCodeUnit >= k_low_surrogate_min && firstCodeUnit <= k_low_surrogate_max) {
            throw_json_error(JsonError::InvalidUnicodeEscape,
                             offsetValue_,
                             "unexpected low surrogate without preceding high surrogate");
        }

        return firstCodeUnit;
    }

    [[nodiscard]] std::uint32_t parse_hex_code_unit() {
        std::uint32_t codeUnitValue = 0U;
        for (std::size_t digitIndex = 0U; digitIndex < 4U; ++digitIndex) {
            if (is_end()) {
                throw_json_error(JsonError::UnexpectedEndOfInput, offsetValue_, "incomplete unicode escape");
            }

            const auto digitChar = read_char();
            const auto digitValue = parse_hex_digit(digitChar);
            if (!digitValue.has_value()) {
                throw_json_error(JsonError::InvalidUnicodeEscape, offsetValue_, "invalid unicode escape digit");
            }
            codeUnitValue = (codeUnitValue * k_hex_base) + *digitValue;
        }

        return codeUnitValue;
    }

    [[nodiscard]] static std::optional<std::uint32_t> parse_hex_digit(char digitChar) {
        if (digitChar >= '0' && digitChar <= '9') {
            return static_cast<std::uint32_t>(digitChar - '0');
        }
        if (digitChar >= 'A' && digitChar <= 'F') {
            return static_cast<std::uint32_t>(digitChar - 'A' + k_decimal_ten);
        }
        if (digitChar >= 'a' && digitChar <= 'f') {
            return static_cast<std::uint32_t>(digitChar - 'a' + k_decimal_ten);
        }
        return std::nullopt;
    }

    static void append_utf8(std::uint32_t codepointValue, std::string& targetText) {
        if (codepointValue <= k_utf8_one_byte_max) {
            targetText.push_back(static_cast<char>(codepointValue));
            return;
        }
        if (codepointValue <= k_utf8_two_byte_max) {
            targetText.push_back(static_cast<char>(
                k_utf8_two_byte_prefix | ((codepointValue >> k_utf8_shift_six) & k_utf8_two_byte_head_mask)));
            targetText.push_back(static_cast<char>(
                k_utf8_continuation_prefix | (codepointValue & k_utf8_continuation_mask)));
            return;
        }
        if (codepointValue <= k_utf8_three_byte_max) {
            targetText.push_back(static_cast<char>(
                k_utf8_three_byte_prefix | ((codepointValue >> k_utf8_shift_twelve) & k_utf8_three_byte_head_mask)));
            targetText.push_back(static_cast<char>(
                k_utf8_continuation_prefix | ((codepointValue >> k_utf8_shift_six) & k_utf8_continuation_mask)));
            targetText.push_back(static_cast<char>(
                k_utf8_continuation_prefix | (codepointValue & k_utf8_continuation_mask)));
            return;
        }

        targetText.push_back(static_cast<char>(
            k_utf8_four_byte_prefix | ((codepointValue >> k_utf8_shift_eighteen) & k_utf8_four_byte_head_mask)));
        targetText.push_back(static_cast<char>(
            k_utf8_continuation_prefix | ((codepointValue >> k_utf8_shift_twelve) & k_utf8_continuation_mask)));
        targetText.push_back(static_cast<char>(
            k_utf8_continuation_prefix | ((codepointValue >> k_utf8_shift_six) & k_utf8_continuation_mask)));
        targetText.push_back(static_cast<char>(
            k_utf8_continuation_prefix | (codepointValue & k_utf8_continuation_mask)));
    }

    [[nodiscard]] double parse_number() {
        const auto numberStartOffset = offsetValue_;

        if (peek_char() == '-') {
            advance();
        }

        if (is_end()) {
            throw_json_error(JsonError::InvalidNumber, numberStartOffset, "invalid number");
        }

        if (peek_char() == '0') {
            advance();
            if (!is_end() && std::isdigit(static_cast<unsigned char>(peek_char())) != 0) {
                throw_json_error(JsonError::InvalidNumber, numberStartOffset, "leading zero not allowed");
            }
        } else {
            parse_digits();
        }

        if (!is_end() && peek_char() == '.') {
            advance();
            parse_digits();
        }

        if (!is_end() && (peek_char() == 'e' || peek_char() == 'E')) {
            advance();
            if (!is_end() && (peek_char() == '+' || peek_char() == '-')) {
                advance();
            }
            parse_digits();
        }

        const auto numberToken = sourceText_.substr(numberStartOffset, offsetValue_ - numberStartOffset);
        try {
            const auto parsedNumber = std::stod(std::string{numberToken});
            if (!std::isfinite(parsedNumber)) {
                throw_json_error(JsonError::InvalidNumber, numberStartOffset, "number must be finite");
            }
            return parsedNumber;
        } catch (const std::exception&) {
            throw_json_error(JsonError::InvalidNumber, numberStartOffset, "invalid number format");
        }
    }

    void parse_digits() {
        if (is_end() || std::isdigit(static_cast<unsigned char>(peek_char())) == 0) {
            throw_json_error(JsonError::InvalidNumber, offsetValue_, "expected digit");
        }

        while (!is_end() && std::isdigit(static_cast<unsigned char>(peek_char())) != 0) {
            advance();
        }
    }

    void consume_keyword(std::string_view keywordText) {
        for (const auto keywordChar : keywordText) {
            if (is_end() || peek_char() != keywordChar) {
                throw_json_error(JsonError::UnexpectedToken, offsetValue_, "unexpected token");
            }
            advance();
        }
    }

    void expect_char(char expectedChar) {
        skip_whitespace();
        if (is_end()) {
            throw_json_error(JsonError::UnexpectedEndOfInput, offsetValue_, "unexpected end of input");
        }
        if (peek_char() != expectedChar) {
            throw_json_error(JsonError::UnexpectedToken,
                             offsetValue_,
                             std::format("expected '{}'", expectedChar));
        }
        advance();
    }

    [[nodiscard]] bool consume_if(char expectedChar) {
        skip_whitespace();
        if (is_end() || peek_char() != expectedChar) {
            return false;
        }
        advance();
        return true;
    }

    void skip_whitespace() {
        while (!is_end() && std::isspace(static_cast<unsigned char>(peek_char())) != 0) {
            advance();
        }
    }

    [[nodiscard]] char peek_char() const {
        return sourceText_[offsetValue_];
    }

    [[nodiscard]] char read_char() {
        const auto currentChar = sourceText_[offsetValue_];
        offsetValue_ += 1U;
        return currentChar;
    }

    void advance() {
        offsetValue_ += 1U;
    }

    [[nodiscard]] bool is_end() const noexcept {
        return offsetValue_ >= sourceText_.size();
    }

    std::string_view sourceText_;
    std::size_t offsetValue_{0U};
};

void append_escaped_json_string(std::string_view sourceText, std::string& outputText) {
    static constexpr std::array<char, 16U> k_hex_digits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    outputText.push_back('"');
    for (const auto currentChar : sourceText) {
        switch (currentChar) {
            case '"':
                outputText += "\\\"";
                break;
            case '\\':
                outputText += "\\\\";
                break;
            case '\b':
                outputText += "\\b";
                break;
            case '\f':
                outputText += "\\f";
                break;
            case '\n':
                outputText += "\\n";
                break;
            case '\r':
                outputText += "\\r";
                break;
            case '\t':
                outputText += "\\t";
                break;
            default: {
                const auto unsignedChar = static_cast<unsigned char>(currentChar);
                if (unsignedChar < k_json_control_boundary) {
                    outputText += "\\u00";
                    outputText.push_back(k_hex_digits[(unsignedChar >> k_high_nibble_shift) & k_hex_nibble_mask]);
                    outputText.push_back(k_hex_digits[unsignedChar & k_hex_nibble_mask]);
                } else {
                    outputText.push_back(currentChar);
                }
                break;
            }
        }
    }
    outputText.push_back('"');
}

void append_number(double numberValue, std::string& outputText) {
    if (!std::isfinite(numberValue)) {
        throw JsonException(JsonError::InvalidNumber, "cannot stringify non-finite number", 0U);
    }

    std::ostringstream stream{};
    stream.precision(static_cast<std::streamsize>(k_number_precision_digits));
    stream << numberValue;
    outputText += stream.str();
}

void append_stringified(const JsonValue& inputValue, std::string& outputText) {
    if (inputValue.is_null()) {
        outputText += "null";
        return;
    }
    if (inputValue.is_boolean()) {
        outputText += inputValue.as_boolean() ? "true" : "false";
        return;
    }
    if (inputValue.is_number()) {
        append_number(inputValue.as_number(), outputText);
        return;
    }
    if (inputValue.is_string()) {
        append_escaped_json_string(inputValue.as_string(), outputText);
        return;
    }
    if (inputValue.is_array()) {
        outputText.push_back('[');
        const auto& arrayValue = inputValue.as_array();
        for (std::size_t indexValue = 0U; indexValue < arrayValue.size(); ++indexValue) {
            if (indexValue > 0U) {
                outputText.push_back(',');
            }
            append_stringified(arrayValue[indexValue], outputText);
        }
        outputText.push_back(']');
        return;
    }

    outputText.push_back('{');
    const auto& objectValue = inputValue.as_object();
    bool firstElement = true;
    for (const auto& [keyName, memberValue] : objectValue) {
        if (!firstElement) {
            outputText.push_back(',');
        }
        firstElement = false;
        append_escaped_json_string(keyName, outputText);
        outputText.push_back(':');
        append_stringified(memberValue, outputText);
    }
    outputText.push_back('}');
}

} // namespace

JsonValue::JsonValue(bool booleanValue)
    : value_(booleanValue) {}

JsonValue::JsonValue(double numberValue)
    : value_(numberValue) {}

JsonValue::JsonValue(std::string stringValue)
    : value_(std::move(stringValue)) {}

JsonValue::JsonValue(const char* stringValue)
    : value_(std::string{stringValue == nullptr ? "" : stringValue}) {}

JsonValue::JsonValue(Object objectValue)
    : value_(std::move(objectValue)) {}

JsonValue::JsonValue(Array arrayValue)
    : value_(std::move(arrayValue)) {}

JsonValue JsonValue::object() {
    return JsonValue{Object{}};
}

JsonValue JsonValue::array() {
    return JsonValue{Array{}};
}

JsonValue JsonValue::parse(std::string_view jsonText) {
    JsonParser parser{jsonText};
    return parser.parse_document();
}

std::optional<JsonValue> JsonValue::try_parse(std::string_view jsonText) noexcept {
    try {
        return parse(jsonText);
    } catch (const JsonException&) {
        return std::nullopt;
    }
}

std::string JsonValue::stringify() const {
    std::string outputText{};
    append_stringified(*this, outputText);
    return outputText;
}

bool JsonValue::is_null() const noexcept {
    return std::holds_alternative<std::monostate>(value_);
}

bool JsonValue::is_boolean() const noexcept {
    return std::holds_alternative<bool>(value_);
}

bool JsonValue::is_number() const noexcept {
    return std::holds_alternative<double>(value_);
}

bool JsonValue::is_string() const noexcept {
    return std::holds_alternative<std::string>(value_);
}

bool JsonValue::is_object() const noexcept {
    return std::holds_alternative<Object>(value_);
}

bool JsonValue::is_array() const noexcept {
    return std::holds_alternative<Array>(value_);
}

bool JsonValue::as_boolean() const {
    if (!is_boolean()) {
        throw JsonException(JsonError::InvalidType, "value is not a boolean", 0U);
    }
    return std::get<bool>(value_);
}

double JsonValue::as_number() const {
    if (!is_number()) {
        throw JsonException(JsonError::InvalidType, "value is not a number", 0U);
    }
    return std::get<double>(value_);
}

const std::string& JsonValue::as_string() const {
    if (!is_string()) {
        throw JsonException(JsonError::InvalidType, "value is not a string", 0U);
    }
    return std::get<std::string>(value_);
}

const JsonValue::Object& JsonValue::as_object() const {
    if (!is_object()) {
        throw JsonException(JsonError::InvalidType, "value is not an object", 0U);
    }
    return std::get<Object>(value_);
}

JsonValue::Object& JsonValue::as_object() {
    if (!is_object()) {
        throw JsonException(JsonError::InvalidType, "value is not an object", 0U);
    }
    return std::get<Object>(value_);
}

const JsonValue::Array& JsonValue::as_array() const {
    if (!is_array()) {
        throw JsonException(JsonError::InvalidType, "value is not an array", 0U);
    }
    return std::get<Array>(value_);
}

JsonValue::Array& JsonValue::as_array() {
    if (!is_array()) {
        throw JsonException(JsonError::InvalidType, "value is not an array", 0U);
    }
    return std::get<Array>(value_);
}

bool JsonValue::contains(std::string_view keyName) const {
    if (!is_object()) {
        return false;
    }

    const auto& objectValue = std::get<Object>(value_);
    return objectValue.contains(std::string{keyName});
}

const JsonValue& JsonValue::at(std::string_view keyName) const {
    if (!is_object()) {
        throw JsonException(JsonError::InvalidType, "value is not an object", 0U);
    }

    const auto& objectValue = std::get<Object>(value_);
    const auto iterator = objectValue.find(std::string{keyName});
    if (iterator == objectValue.end()) {
        throw JsonException(JsonError::MissingKey,
                            std::format("missing key '{}'", keyName),
                            0U);
    }
    return iterator->second;
}

JsonValue& JsonValue::at(std::string_view keyName) {
    if (!is_object()) {
        throw JsonException(JsonError::InvalidType, "value is not an object", 0U);
    }

    auto& objectValue = std::get<Object>(value_);
    const auto iterator = objectValue.find(std::string{keyName});
    if (iterator == objectValue.end()) {
        throw JsonException(JsonError::MissingKey,
                            std::format("missing key '{}'", keyName),
                            0U);
    }
    return iterator->second;
}

const JsonValue& JsonValue::at(std::size_t indexValue) const {
    if (!is_array()) {
        throw JsonException(JsonError::InvalidType, "value is not an array", 0U);
    }

    const auto& arrayValue = std::get<Array>(value_);
    if (indexValue >= arrayValue.size()) {
        throw JsonException(JsonError::IndexOutOfRange,
                            std::format("index {} out of range", indexValue),
                            0U);
    }
    return arrayValue[indexValue];
}

JsonValue& JsonValue::at(std::size_t indexValue) {
    if (!is_array()) {
        throw JsonException(JsonError::InvalidType, "value is not an array", 0U);
    }

    auto& arrayValue = std::get<Array>(value_);
    if (indexValue >= arrayValue.size()) {
        throw JsonException(JsonError::IndexOutOfRange,
                            std::format("index {} out of range", indexValue),
                            0U);
    }
    return arrayValue[indexValue];
}

JsonValue& JsonValue::operator[](std::string_view keyName) {
    if (is_null()) {
        value_ = Object{};
    }
    if (!is_object()) {
        throw JsonException(JsonError::InvalidType,
                            "key access requires object or null value",
                            0U);
    }

    auto& objectValue = std::get<Object>(value_);
    return objectValue[std::string{keyName}];
}

JsonValue& JsonValue::operator[](std::size_t indexValue) {
    if (is_null()) {
        value_ = Array{};
    }
    if (!is_array()) {
        throw JsonException(JsonError::InvalidType,
                            "index access requires array or null value",
                            0U);
    }

    auto& arrayValue = std::get<Array>(value_);
    if (indexValue >= arrayValue.size()) {
        arrayValue.resize(indexValue + 1U);
    }
    return arrayValue[indexValue];
}

void JsonValue::push_back(JsonValue elementValue) {
    if (is_null()) {
        value_ = Array{};
    }
    if (!is_array()) {
        throw JsonException(JsonError::InvalidType,
                            "push_back requires array or null value",
                            0U);
    }

    auto& arrayValue = std::get<Array>(value_);
    arrayValue.push_back(std::move(elementValue));
}

std::size_t JsonValue::size() const noexcept {
    if (is_object()) {
        return std::get<Object>(value_).size();
    }
    if (is_array()) {
        return std::get<Array>(value_).size();
    }
    return 0U;
}

} // namespace mqtt::json
