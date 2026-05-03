#pragma once

#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "yaha/automation/expression_ast.h"

namespace yaha {

/**
 * @brief Generic structured input node for rules tree traversal.
 */
struct RuleTreeNode {
    using Object = std::map<std::string, RuleTreeNode>;
    using Array = std::vector<RuleTreeNode>;

    std::variant<std::monostate, bool, double, std::string, Object, Array> value;

    RuleTreeNode() = default;
    RuleTreeNode(bool booleanValue);
    RuleTreeNode(double numberValue);
    RuleTreeNode(std::string textValue);
    RuleTreeNode(const char* textValue);
    RuleTreeNode(Object objectValue);
    RuleTreeNode(Array arrayValue);

    [[nodiscard]] bool isString() const noexcept;
    [[nodiscard]] bool isObject() const noexcept;
    [[nodiscard]] bool isArray() const noexcept;

    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] const Object& asObject() const;
    [[nodiscard]] const Array& asArray() const;
};

struct ParsedScriptSnippet {
    FieldScriptAst ast;
    std::set<std::string> externalVariables;
};

struct RulesTreeParseResult {
    std::map<std::string, ParsedScriptSnippet> snippetsByPath;
    std::set<std::string> externalVariables;
    std::vector<ParseError> errors;

    [[nodiscard]] bool success() const noexcept;
};

/**
 * @brief Parses all expression snippets from a structured rules tree.
 */
class RulesTreeParser {
public:
    /**
     * @brief Traverses full tree and parses all expression-bearing fields.
     *
     * Recognized expression fields: `check`, `value`, `time`.
     *
     * @param root Structured input rules tree.
     * @return Parsed snippets addressable by slash paths.
     */
    [[nodiscard]] static RulesTreeParseResult parse(const RuleTreeNode& root);
};

} // namespace yaha
