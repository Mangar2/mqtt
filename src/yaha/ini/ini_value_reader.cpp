#include "yaha/ini/ini_value_reader.h"

#include <cstdlib>

namespace yaha {

std::optional<std::string> iniLookupLastValue(
    const IniDocument& document,
    std::string_view sectionName,
    std::string_view key) {
    return document.lastValue(sectionName, key);
}

bool iniTryParseUnsigned(
    const std::string& text,
    const std::uint64_t minValue,
    const std::uint64_t maxValue,
    std::uint64_t& output) {
    if (text.empty()) {
        return false;
    }

    char* endPointer = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &endPointer, 10);
    if (endPointer == nullptr || *endPointer != '\0') {
        return false;
    }

    if (parsed < minValue || parsed > maxValue) {
        return false;
    }

    output = static_cast<std::uint64_t>(parsed);
    return true;
}

bool iniTryReadUnsigned(
    const IniDocument& document,
    const std::string_view sectionName,
    const std::string_view key,
    const std::uint64_t minValue,
    const std::uint64_t maxValue,
    std::uint64_t& output,
    const std::string_view errorFieldName,
    std::string& errorMessage) {
    const auto maybeValue = iniLookupLastValue(document, sectionName, key);
    if (!maybeValue.has_value()) {
        return true;
    }

    if (!iniTryParseUnsigned(*maybeValue, minValue, maxValue, output)) {
        errorMessage = "invalid " + std::string{errorFieldName};
        return false;
    }

    return true;
}

} // namespace yaha
