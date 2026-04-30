#pragma once

/**
 * @file ini_value_reader.h
 * @brief Generic helper functions for reading typed values from IniDocument.
 */

#include "yaha/ini/ini_document.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace yaha {

/**
 * @brief Returns the last value for section/key from an INI document.
 * @param document Parsed INI document.
 * @param sectionName Section name.
 * @param key Key name.
 * @return Last value when present, otherwise nullopt.
 */
[[nodiscard]] std::optional<std::string> iniLookupLastValue(
    const IniDocument& document,
    std::string_view sectionName,
    std::string_view key);

/**
 * @brief Parses one unsigned integer with inclusive bounds.
 * @param text Input text.
 * @param minValue Lower inclusive bound.
 * @param maxValue Upper inclusive bound.
 * @param output Parsed result on success.
 * @return True when parsing and bounds validation succeeded.
 */
[[nodiscard]] bool iniTryParseUnsigned(
    const std::string& text,
    std::uint64_t minValue,
    std::uint64_t maxValue,
    std::uint64_t& output);

/**
 * @brief Reads and validates one optional unsigned section/key value.
 * @param document Parsed INI document.
 * @param sectionName Section name.
 * @param key Key name.
 * @param minValue Lower inclusive bound.
 * @param maxValue Upper inclusive bound.
 * @param output Parsed result output.
 * @param errorFieldName Field name used in error message.
 * @param errorMessage Human-readable parser error text.
 * @return True when key is missing or valid; false on invalid value.
 */
[[nodiscard]] bool iniTryReadUnsigned(
    const IniDocument& document,
    std::string_view sectionName,
    std::string_view key,
    std::uint64_t minValue,
    std::uint64_t maxValue,
    std::uint64_t& output,
    std::string_view errorFieldName,
    std::string& errorMessage);

} // namespace yaha
