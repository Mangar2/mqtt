#include "yaha/automation/expression_tokenizer.h"

#include <cctype>
#include <stdexcept>

namespace yaha {
namespace {

[[nodiscard]] bool isWhitespace(const char currentChar) noexcept {
    return currentChar == ' ' || currentChar == '\t';
}

[[nodiscard]] bool isLineBreak(const char currentChar) noexcept {
    return currentChar == '\n' || currentChar == '\r';
}

[[nodiscard]] bool isSingleCharToken(const char currentChar) noexcept {
    switch (currentChar) {
    case '(':
    case ')':
    case ',':
    case ':':
    case '+':
    case '-':
    case '=':
    case '>':
    case '<':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool startsTwoCharToken(const std::string& input, const std::size_t index) {
    if (index + 1U >= input.size()) {
        return false;
    }

    const char first = input[index];
    const char second = input[index + 1U];
    return (first == '!' && second == '=')
        || (first == '<' && second == '>')
        || (first == '>' && second == '=')
        || (first == '<' && second == '=');
}

[[nodiscard]] bool isTokenBoundaryChar(const char currentChar) noexcept {
    return isLineBreak(currentChar)
        || currentChar == '\'' || currentChar == '"'
        || currentChar == '(' || currentChar == ')' || currentChar == ',' || currentChar == ':'
        || currentChar == '=' || currentChar == '>' || currentChar == '<' || currentChar == '!'
        || currentChar == '+' || currentChar == '-';
}

[[nodiscard]] std::size_t skipSpaces(const std::string& input, const std::size_t index) {
    std::size_t probe = index;
    while (probe < input.size() && isWhitespace(input[probe])) {
        probe += 1U;
    }
    return probe;
}

[[nodiscard]] bool startsWithWord(const std::string& input, const std::size_t index,
                                  const std::string& word) {
    if (index + word.size() > input.size()) {
        return false;
    }
    for (std::size_t i = 0U; i < word.size(); ++i) {
        if (input[index + i] != word[i]) {
            return false;
        }
    }

    const std::size_t after = index + word.size();
    if (after >= input.size()) {
        return true;
    }
    const char separator = input[after];
    return isWhitespace(separator) || isTokenBoundaryChar(separator) || startsTwoCharToken(input, after);
}

[[nodiscard]] bool firstWordContainsSlash(const std::string& input, const std::size_t index) {
    std::size_t probe = index;
    while (probe < input.size()) {
        const char current = input[probe];
        if (isWhitespace(current) || isTokenBoundaryChar(current) || startsTwoCharToken(input, probe)) {
            break;
        }
        if (current == '/') {
            return true;
        }
        probe += 1U;
    }
    return false;
}

[[nodiscard]] bool startsLogicalOperatorBoundary(const std::string& input, const std::size_t index) {
    std::size_t probe = skipSpaces(input, index);
    std::string operatorWord;
    if (startsWithWord(input, probe, "and")) {
        operatorWord = "and";
    } else if (startsWithWord(input, probe, "or")) {
        operatorWord = "or";
    } else {
        return false;
    }

    probe += operatorWord.size();
    probe = skipSpaces(input, probe);
    if (probe >= input.size()) {
        return true;
    }

    const char next = input[probe];
    if (next == '\'' || next == '"' || next == '$' || next == '/') {
        return true;
    }

    return firstWordContainsSlash(input, probe);
}

void appendQuotedToken(const std::string& input, std::size_t* index,
                       std::vector<std::string>* tokens) {
    const char quote = input[*index];
    const std::size_t start = *index;
    *index += 1U;

    bool escaped = false;
    while (*index < input.size()) {
        const char current = input[*index];
        if (!escaped && current == quote) {
            *index += 1U;
            tokens->push_back(input.substr(start, *index - start));
            return;
        }

        escaped = !escaped && current == '\\';

        *index += 1U;
    }

    throw std::invalid_argument("unterminated quoted string in expression script");
}

void appendBareToken(const std::string& input, std::size_t* index,
                     std::vector<std::string>* tokens) {
    const std::size_t start = *index;
    bool containsSlash = false;

    while (*index < input.size()) {
        const char current = input[*index];

        if (current == '/') {
            containsSlash = true;
            *index += 1U;
            continue;
        }

        if (isWhitespace(current)) {
            if (!containsSlash) {
                break;
            }

            if (startsLogicalOperatorBoundary(input, *index)) {
                break;
            }

            const std::size_t next = skipSpaces(input, *index);
            if (next >= input.size() || isTokenBoundaryChar(input[next]) || startsTwoCharToken(input, next)) {
                break;
            }

            *index = next;
            continue;
        }

        if (isTokenBoundaryChar(current) || startsTwoCharToken(input, *index)) {
            break;
        }

        *index += 1U;
    }

    if (*index > start) {
        tokens->push_back(input.substr(start, *index - start));
    }
}

[[nodiscard]] bool consumeLineBreakToken(
    const std::string& input,
    std::size_t* index,
    std::vector<std::string>* tokens) {
    if (input[*index] == '\r') {
        if (*index + 1U < input.size() && input[*index + 1U] == '\n') {
            *index += 2U;
        } else {
            *index += 1U;
        }
        tokens->emplace_back("\\n");
        return true;
    }

    if (input[*index] == '\n') {
        *index += 1U;
        tokens->emplace_back("\\n");
        return true;
    }

    return false;
}

[[nodiscard]] bool consumeQuotedToken(
    const std::string& input,
    std::size_t* index,
    std::vector<std::string>* tokens) {
    const char current = input[*index];
    if (current != '\'' && current != '"') {
        return false;
    }
    appendQuotedToken(input, index, tokens);
    return true;
}

[[nodiscard]] bool consumeTwoCharOperatorToken(
    const std::string& input,
    std::size_t* index,
    std::vector<std::string>* tokens) {
    if (!startsTwoCharToken(input, *index)) {
        return false;
    }
    tokens->push_back(input.substr(*index, 2U));
    *index += 2U;
    return true;
}

[[nodiscard]] bool consumeNegativeNumberToken(
    const std::string& input,
    std::size_t* index,
    std::vector<std::string>* tokens) {
    if (input[*index] != '-' || *index + 1U >= input.size()
        || std::isdigit(static_cast<unsigned char>(input[*index + 1U])) == 0) {
        return false;
    }

    const std::size_t numberStart = *index;
    *index += 1U;
    while (*index < input.size() && std::isdigit(static_cast<unsigned char>(input[*index])) != 0) {
        *index += 1U;
    }

    if (*index < input.size() && input[*index] == '.') {
        *index += 1U;
        while (*index < input.size() && std::isdigit(static_cast<unsigned char>(input[*index])) != 0) {
            *index += 1U;
        }
    }

    tokens->push_back(input.substr(numberStart, *index - numberStart));
    return true;
}

[[nodiscard]] bool consumeSingleCharToken(
    const std::string& input,
    std::size_t* index,
    std::vector<std::string>* tokens) {
    const char current = input[*index];
    if (!isSingleCharToken(current)) {
        return false;
    }

    if (consumeNegativeNumberToken(input, index, tokens)) {
        return true;
    }

    tokens->push_back(input.substr(*index, 1U));
    *index += 1U;
    return true;
}

} // namespace

std::vector<std::string> ExpressionTokenizer::tokenize(const std::string& program) {
    std::vector<std::string> tokens;
    std::size_t index = 0U;

    while (index < program.size()) {
        const char current = program[index];

        if (isWhitespace(current)) {
            index += 1U;
            continue;
        }

        if (consumeLineBreakToken(program, &index, &tokens)) {
            continue;
        }

        if (consumeQuotedToken(program, &index, &tokens)) {
            continue;
        }

        if (consumeTwoCharOperatorToken(program, &index, &tokens)) {
            continue;
        }

        if (consumeSingleCharToken(program, &index, &tokens)) {
            continue;
        }

        if (current == '!') {
            throw std::invalid_argument("invalid token '!' in expression script");
        }

        appendBareToken(program, &index, &tokens);
    }

    return tokens;
}

} // namespace yaha
