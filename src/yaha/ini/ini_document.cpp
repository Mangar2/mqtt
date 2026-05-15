#include "yaha/ini/ini_document.h"

#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace yaha {

namespace {

std::string trimCopy(std::string value) {
    std::size_t beginIndex = 0U;
    while (beginIndex < value.size() &&
           std::isspace(static_cast<unsigned char>(value[beginIndex])) != 0) {
        beginIndex += 1U;
    }

    if (beginIndex == value.size()) {
        return "";
    }

    std::size_t endIndex = value.size();
    while (endIndex > beginIndex &&
           std::isspace(static_cast<unsigned char>(value[endIndex - 1U])) != 0) {
        endIndex -= 1U;
    }

    return value.substr(beginIndex, endIndex - beginIndex);
}

std::string stripComment(std::string line) {
    if (!line.empty() && line.front() == '#') {
        return "";
    }

    bool inQuotes = false;
    for (std::size_t index = 0U; index < line.size(); ++index) {
        const char currentChar = line[index];
        if (currentChar == '"') {
            inQuotes = !inQuotes;
            continue;
        }

        if (!inQuotes && currentChar == ';') {
            const bool atLineStart = index == 0U;
            const bool afterWhitespace = !atLineStart &&
                std::isspace(static_cast<unsigned char>(line[index - 1U])) != 0;
            if (atLineStart || afterWhitespace) {
                return line.substr(0U, index);
            }
        }
    }

    return line;
}

[[nodiscard]] std::string makeFieldName(
    const std::string_view sectionName,
    const std::string_view key) {
    return std::format("{}.{}", sectionName, key);
}

[[nodiscard]] std::runtime_error makeLoadError(
    const std::filesystem::path& filePath,
    const std::string_view reason,
    const std::error_code systemError) {
    return std::runtime_error{std::format(
        "unable to load config file '{}' (reason='{}', system_error_id={}, system_error='{}')",
        filePath.string(),
        reason,
        systemError.value(),
        systemError.message())};
}

} // namespace

void IniDocument::Section::addEntry(std::string key, std::string value) {
    valuesByKey_[key].push_back(value);
    entries_.push_back(Entry{.key = std::move(key), .value = std::move(value)});
}

const std::vector<IniDocument::Entry>& IniDocument::Section::entries() const {
    return entries_;
}

std::optional<std::vector<std::string>> IniDocument::Section::valuesForKey(std::string_view key) const {
    const auto iterator = valuesByKey_.find(std::string{key});
    if (iterator == valuesByKey_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

std::optional<std::string> IniDocument::Section::lastValueForKey(std::string_view key) const {
    const auto maybeValues = valuesForKey(key);
    if (!maybeValues.has_value() || maybeValues->empty()) {
        return std::nullopt;
    }

    return maybeValues->back();
}

IniDocument IniDocument::loadFromFile(const std::filesystem::path& filePath) {
    std::ifstream input{filePath};
    if (!input.is_open()) {
        const auto systemError = std::error_code{errno, std::generic_category()};
        throw makeLoadError(filePath, "open failed", systemError);
    }

    IniDocument parsed{};
    std::string currentSection{};
    std::string line{};
    std::uint32_t lineNumber = 0U;

    while (std::getline(input, line)) {
        lineNumber += 1U;

        std::string cleaned = trimCopy(stripComment(std::move(line)));
        if (cleaned.empty()) {
            continue;
        }

        if (cleaned.front() == '[' && cleaned.back() == ']') {
            currentSection = trimCopy(cleaned.substr(1U, cleaned.size() - 2U));
            if (currentSection.empty()) {
                throw std::runtime_error{std::format(
                    "invalid config syntax in '{}' at line {}: empty section name",
                    filePath.string(),
                    lineNumber)};
            }
            continue;
        }

        const std::size_t delimiterPosition = cleaned.find('=');
        if (delimiterPosition == std::string::npos) {
            throw std::runtime_error{std::format(
                "invalid config syntax in '{}' at line {}: missing '=' in '{}'",
                filePath.string(),
                lineNumber,
                cleaned)};
        }

        const std::string key = trimCopy(cleaned.substr(0U, delimiterPosition));
        const std::string value = trimCopy(cleaned.substr(delimiterPosition + 1U));
        if (key.empty()) {
            throw std::runtime_error{std::format(
                "invalid config syntax in '{}' at line {}: empty key",
                filePath.string(),
                lineNumber)};
        }

        parsed.sections_[currentSection].addEntry(key, value);
    }

    if (!input.eof()) {
        const auto systemError = std::error_code{errno, std::generic_category()};
        throw makeLoadError(filePath, "read failed", systemError);
    }

    return parsed;
}

const IniDocument::Section* IniDocument::findSection(std::string_view sectionName) const {
    const auto iterator = sections_.find(std::string{sectionName});
    if (iterator == sections_.end()) {
        return nullptr;
    }

    return &iterator->second;
}

std::optional<std::string> IniDocument::lastValue(
    std::string_view sectionName,
    std::string_view key) const {
    const Section* section = findSection(sectionName);
    if (section == nullptr) {
        return std::nullopt;
    }

    return section->lastValueForKey(key);
}

std::optional<std::uint64_t> IniDocument::parseUnsigned(
    const std::string_view text,
    const std::uint64_t minValue,
    const std::uint64_t maxValue) {
    if (text.empty()) {
        return std::nullopt;
    }

    auto parsed = std::uint64_t{0U};
    const auto* const beginIterator = text.data();
    const auto* const endIterator = text.data() + text.size();
    const auto [nextIterator, parseError] = std::from_chars(beginIterator, endIterator, parsed, 10);
    if (parseError != std::errc{} || nextIterator != endIterator) {
        return std::nullopt;
    }

    if (parsed < minValue || parsed > maxValue) {
        return std::nullopt;
    }

    return parsed;
}

std::pair<std::optional<std::uint64_t>, std::string> IniDocument::readUnsigned(
    const std::string_view sectionName,
    const std::string_view key,
    const std::uint64_t minValue,
    const std::uint64_t maxValue) const {
    const auto maybeValue = lastValue(sectionName, key);
    if (!maybeValue.has_value()) {
        return {std::nullopt, ""};
    }

    const auto parsed = parseUnsigned(*maybeValue, minValue, maxValue);
    if (!parsed.has_value()) {
        const auto fieldName = makeFieldName(sectionName, key);
        return {
            std::nullopt,
            std::format(
                "invalid unsigned value for '{}' (expected {}..{}, got '{}')",
                fieldName,
                minValue,
                maxValue,
                *maybeValue)};
    }

    return {*parsed, ""};
}

std::pair<std::optional<bool>, std::string> IniDocument::readBool(
    const std::string_view sectionName,
    const std::string_view key) const {
    const auto maybeValue = lastValue(sectionName, key);
    if (!maybeValue.has_value()) {
        return {std::nullopt, ""};
    }

    const auto& text = *maybeValue;
    if (text == "true" || text == "1" || text == "yes" || text == "on") {
        return {true, ""};
    }

    if (text == "false" || text == "0" || text == "no" || text == "off") {
        return {false, ""};
    }

    const auto fieldName = makeFieldName(sectionName, key);
    return {
        std::nullopt,
        std::format("invalid boolean value for '{}' (got '{}')", fieldName, text)};
}

} // namespace yaha
