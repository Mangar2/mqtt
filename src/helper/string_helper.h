#pragma once

/**
 * @file string_helper.h
 * @brief Generic, domain-independent string utilities shared by broker and YAHA modules.
 */

#include <string>
#include <string_view>
#include <vector>

namespace mqtt::helper {

/**
 * @brief Returns an ASCII-lowercased copy of the given text.
 * @param text Input text to convert.
 * @return New string with every ASCII letter lowercased; non-ASCII bytes are left unchanged.
 */
[[nodiscard]] std::string toLower(std::string_view text);

/**
 * @brief Returns a copy of the given text with leading and trailing whitespace removed.
 * @param text Input text to trim.
 * @return New string without leading/trailing whitespace as classified by `std::isspace`.
 */
[[nodiscard]] std::string trim(std::string_view text);

/**
 * @brief Splits text into trimmed tokens separated by a single delimiter character.
 * @param text Input text to split.
 * @param delimiter Character that separates tokens.
 * @return Tokens in order of appearance, each trimmed of leading/trailing whitespace. A trailing
 *         delimiter with nothing after it does not produce an extra empty token; two consecutive
 *         delimiters produce an empty token between them.
 */
[[nodiscard]] std::vector<std::string> split(std::string_view text, char delimiter);

} // namespace mqtt::helper
