#include "yaha/automation/rules_tree_json_reader.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace yaha {
namespace {

class JsonParser {
public:
    explicit JsonParser(std::string input)
        : input_(std::move(input)) {
    }

    [[nodiscard]] RuleTreeJsonReadResult parse() {
        RuleTreeJsonReadResult result;

        try {
            skipWhitespace();
            result.root = parseValue();
            skipWhitespace();
            if (!atEnd()) {
                throw makeError("unexpected trailing characters");
            }
            result.success = true;
            return result;
        } catch (const RuleTreeJsonReadError& error) {
            result.errors.push_back(error);
            return result;
        }
    }

private:
    [[nodiscard]] bool atEnd() const noexcept {
        return index_ >= input_.size();
    }

    [[nodiscard]] char peek() const {
        return input_[index_];
    }

    char consume() {
        const char current = input_[index_];
        index_ += 1U;
        if (current == '\n') {
            line_ += 1U;
            column_ = 1U;
        } else {
            column_ += 1U;
        }
        return current;
    }

    void skipWhitespace() {
        while (!atEnd() && std::isspace(static_cast<unsigned char>(peek())) != 0) {
            consume();
        }
    }

    [[nodiscard]] RuleTreeJsonReadError makeError(const std::string& message) const {
        return RuleTreeJsonReadError{.message = message, .line = line_, .column = column_};
    }

    [[nodiscard]] RuleTreeNode parseValue() {
        if (atEnd()) {
            throw makeError("unexpected end of json");
        }

        const char current = peek();
        if (current == '{') {
            return parseObject();
        }
        if (current == '[') {
            return parseArray();
        }
        if (current == '"') {
            return RuleTreeNode{parseString()};
        }
        if (current == 't') {
            parseKeyword("true");
            return RuleTreeNode{true};
        }
        if (current == 'f') {
            parseKeyword("false");
            return RuleTreeNode{false};
        }
        if (current == 'n') {
            parseKeyword("null");
            return RuleTreeNode{};
        }
        if (current == '-' || std::isdigit(static_cast<unsigned char>(current)) != 0) {
            return RuleTreeNode{parseNumber()};
        }

        throw makeError("invalid json token");
    }

    [[nodiscard]] RuleTreeNode parseObject() {
        consume();
        skipWhitespace();

        RuleTreeNode::Object objectValue;
        if (!atEnd() && peek() == '}') {
            consume();
            return RuleTreeNode{std::move(objectValue)};
        }

        while (true) {
            skipWhitespace();
            if (atEnd() || peek() != '"') {
                throw makeError("object key must be a string");
            }
            const std::string key = parseString();

            skipWhitespace();
            if (atEnd() || consume() != ':') {
                throw makeError("missing ':' after object key");
            }

            skipWhitespace();
            objectValue.insert({key, parseValue()});

            skipWhitespace();
            if (atEnd()) {
                throw makeError("unexpected end in object");
            }
            const char separator = consume();
            if (separator == '}') {
                break;
            }
            if (separator != ',') {
                throw makeError("object requires ',' or '}'");
            }
        }

        return RuleTreeNode{std::move(objectValue)};
    }

    [[nodiscard]] RuleTreeNode parseArray() {
        consume();
        skipWhitespace();

        RuleTreeNode::Array arrayValue;
        if (!atEnd() && peek() == ']') {
            consume();
            return RuleTreeNode{std::move(arrayValue)};
        }

        while (true) {
            skipWhitespace();
            arrayValue.push_back(parseValue());

            skipWhitespace();
            if (atEnd()) {
                throw makeError("unexpected end in array");
            }
            const char separator = consume();
            if (separator == ']') {
                break;
            }
            if (separator != ',') {
                throw makeError("array requires ',' or ']'");
            }
        }

        return RuleTreeNode{std::move(arrayValue)};
    }

    [[nodiscard]] std::string parseString() {
        if (consume() != '"') {
            throw makeError("string must start with quote");
        }

        std::string text;
        while (!atEnd()) {
            const char current = consume();
            if (current == '"') {
                return text;
            }

            if (current == '\\') {
                if (atEnd()) {
                    throw makeError("invalid escape sequence");
                }
                const char escaped = consume();
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    text.push_back(escaped);
                    break;
                case 'b':
                    text.push_back('\b');
                    break;
                case 'f':
                    text.push_back('\f');
                    break;
                case 'n':
                    text.push_back('\n');
                    break;
                case 'r':
                    text.push_back('\r');
                    break;
                case 't':
                    text.push_back('\t');
                    break;
                case 'u':
                    throw makeError("unicode escapes are not supported in this reader");
                default:
                    throw makeError("invalid escape sequence");
                }
                continue;
            }

            text.push_back(current);
        }

        throw makeError("unterminated string");
    }

    [[nodiscard]] double parseNumber() {
        const std::size_t start = index_;

        if (!atEnd() && peek() == '-') {
            consume();
        }

        parseIntegerPart();
        parseFractionPartIfPresent();
        parseExponentPartIfPresent();

        const std::string text = input_.substr(start, index_ - start);
        try {
            return std::stod(text);
        } catch (...) {
            throw makeError("number conversion failed");
        }
    }

    void parseIntegerPart() {
        if (atEnd() || std::isdigit(static_cast<unsigned char>(peek())) == 0) {
            throw makeError("invalid number");
        }

        if (peek() == '0') {
            consume();
            return;
        }

        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            consume();
        }
    }

    void parseFractionPartIfPresent() {
        if (atEnd() || peek() != '.') {
            return;
        }

        consume();
        if (atEnd() || std::isdigit(static_cast<unsigned char>(peek())) == 0) {
            throw makeError("invalid number fraction");
        }

        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            consume();
        }
    }

    void parseExponentPartIfPresent() {
        if (atEnd() || (peek() != 'e' && peek() != 'E')) {
            return;
        }

        consume();
        if (!atEnd() && (peek() == '+' || peek() == '-')) {
            consume();
        }

        if (atEnd() || std::isdigit(static_cast<unsigned char>(peek())) == 0) {
            throw makeError("invalid number exponent");
        }

        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            consume();
        }
    }

    void parseKeyword(const char* keyword) {
        for (std::size_t i = 0U; keyword[i] != '\0'; ++i) {
            if (atEnd() || consume() != keyword[i]) {
                throw makeError("invalid keyword");
            }
        }
    }

    std::string input_;
    std::size_t index_{0U};
    std::size_t line_{1U};
    std::size_t column_{1U};
};

[[nodiscard]] std::string readFileText(const std::string& filePath) {
    std::ifstream stream{filePath};
    if (!stream.is_open()) {
        throw std::runtime_error("failed to open json file: " + filePath);
    }

    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

} // namespace

RuleTreeJsonReadResult RulesTreeJsonReader::parseJsonText(const std::string& jsonText) {
    JsonParser parser{jsonText};
    return parser.parse();
}

RuleTreeJsonReadResult RulesTreeJsonReader::parseJsonFile(const std::string& filePath) {
    RuleTreeJsonReadResult result;

    try {
        return parseJsonText(readFileText(filePath));
    } catch (const std::exception& exception) {
        result.errors.push_back(RuleTreeJsonReadError{.message = exception.what(), .line = 0U, .column = 0U});
        return result;
    }
}

} // namespace yaha
