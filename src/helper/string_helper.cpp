#include "helper/string_helper.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mqtt::helper {

std::string toLower(const std::string_view text) {
    std::string result{text};
    std::ranges::transform(result, result.begin(), [](const unsigned char characterValue) {
        return static_cast<char>(std::tolower(characterValue));
    });
    return result;
}

std::string trim(const std::string_view text) {
    std::size_t beginIndex = 0U;
    while (beginIndex < text.size() && std::isspace(static_cast<unsigned char>(text[beginIndex])) != 0) {
        ++beginIndex;
    }

    std::size_t endIndex = text.size();
    while (endIndex > beginIndex && std::isspace(static_cast<unsigned char>(text[endIndex - 1U])) != 0) {
        --endIndex;
    }

    return std::string{text.substr(beginIndex, endIndex - beginIndex)};
}

std::vector<std::string> split(const std::string_view text, const char delimiter) {
    std::vector<std::string> tokens{};
    std::istringstream stream{std::string{text}};
    std::string token{};
    while (std::getline(stream, token, delimiter)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

} // namespace mqtt::helper
