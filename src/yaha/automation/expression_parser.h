#pragma once

#include <string>

#include "yaha/automation/expression_ast.h"

namespace yaha {

/**
 * @brief Parses one expression field script into AST and external variable list.
 */
class ExpressionParser {
public:
    /**
     * @brief Parses one field script according to automation EBNF subset.
     *
     * @param script Source script in python-style DSL.
     * @return Parse result including AST, external variables and parse errors.
     */
    [[nodiscard]] static ExpressionParseResult parse(const std::string& script);
};

} // namespace yaha
