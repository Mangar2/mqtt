#include "yaha/ini/ini_document.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

namespace yaha {

namespace {

std::string trimCopy(std::string value) {
    auto notSpace = [](unsigned char character) {
        return std::isspace(character) == 0;
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string stripComment(std::string line) {
    const std::size_t semicolonPosition = line.find(';');
    const std::size_t commentPosition =
        semicolonPosition == std::string::npos ? line.size() : semicolonPosition;
    return line.substr(0U, commentPosition);
}

} // namespace

void IniDocument::Section::addEntry(std::string key, std::string value) {
    valuesByKey_[key].push_back(value);
    entries_.push_back(Entry{std::move(key), std::move(value)});
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

bool IniDocument::tryLoadFromFile(
    const std::filesystem::path& filePath,
    IniDocument& output,
    std::string& errorMessage) {
    std::ifstream input{filePath};
    if (!input.is_open()) {
        errorMessage = "unable to open config file: " + filePath.string();
        return false;
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
                errorMessage = "empty section name at line " + std::to_string(lineNumber);
                return false;
            }
            continue;
        }

        const std::size_t delimiterPosition = cleaned.find('=');
        if (delimiterPosition == std::string::npos) {
            errorMessage = "missing '=' at line " + std::to_string(lineNumber);
            return false;
        }

        const std::string key = trimCopy(cleaned.substr(0U, delimiterPosition));
        const std::string value = trimCopy(cleaned.substr(delimiterPosition + 1U));
        if (key.empty()) {
            errorMessage = "empty key at line " + std::to_string(lineNumber);
            return false;
        }

        parsed.sections_[currentSection].addEntry(key, value);
    }

    output = std::move(parsed);
    return true;
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

} // namespace yaha
