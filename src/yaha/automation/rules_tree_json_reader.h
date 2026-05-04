#pragma once

#include <string>
#include <vector>

#include "yaha/automation/rules_tree_parser.h"

namespace yaha {

struct RuleTreeJsonReadError {
    std::string message;
    std::size_t line{0U};
    std::size_t column{0U};
};

struct RuleTreeJsonReadResult {
    bool success{false};
    RuleTreeNode root{};
    std::vector<RuleTreeJsonReadError> errors;
};

class RulesTreeJsonReader {
public:
    [[nodiscard]] static RuleTreeJsonReadResult parseJsonText(const std::string& jsonText);
    [[nodiscard]] static RuleTreeJsonReadResult parseJsonFile(const std::string& filePath);
};

} // namespace yaha
