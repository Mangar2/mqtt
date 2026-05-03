#pragma once

#include <string>
#include <vector>

namespace yaha {

/**
 * @brief Tokenizes YAHA expression DSL source into lexical token strings.
 *
 * Token output keeps lexical values as strings and preserves line breaks as
 * explicit "\n" tokens so higher parser stages can separate declaration lines.
 */
class ExpressionTokenizer {
public:
    /**
     * @brief Splits one expression script into ordered token strings.
     *
     * @param program Full expression script source text.
     * @return Ordered token sequence.
     * @throws std::invalid_argument If a quoted string is not terminated.
     */
    [[nodiscard]] static std::vector<std::string> tokenize(const std::string& program);
};

} // namespace yaha
