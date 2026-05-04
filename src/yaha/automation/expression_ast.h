#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace yaha {

enum class UnaryOperator : std::uint8_t {
    Not
};

enum class BinaryOperator : std::uint8_t {
    Add,
    Sub,
    Eq,
    Neq,
    Gt,
    Lt,
    Ge,
    Le,
    And,
    Or
};

struct ExprNode;
using ExprPtr = std::shared_ptr<ExprNode>;

struct LiteralNode {
    std::variant<std::string, double> value;
};

struct VariableRefNode {
    std::string name;
};

struct IdentifierNode {
    std::string name;
};

struct UnaryOpNode {
    UnaryOperator op;
    ExprPtr operand;
};

struct BinaryOpNode {
    BinaryOperator op;
    ExprPtr left;
    ExprPtr right;
};

struct IfCallNode {
    ExprPtr condition;
    ExprPtr trueValue;
    ExprPtr falseValue;
};

struct MapCallNode {
    std::string name;
    ExprPtr selector;
};

struct MapKeyAst {
    bool isDefault{false};
    std::string token;
};

struct MapEntryAst {
    MapKeyAst key;
    ExprPtr value;
};

struct MapLiteralNode {
    std::vector<MapEntryAst> entries;
};

struct ExprNode {
    std::variant<
        LiteralNode,
        VariableRefNode,
        IdentifierNode,
        UnaryOpNode,
        BinaryOpNode,
        IfCallNode,
        MapCallNode,
        MapLiteralNode
    > node;
};

struct MapDeclarationAst {
    std::string name;
    std::vector<MapEntryAst> entries;
};

struct FieldScriptAst {
    std::vector<MapDeclarationAst> declarations;
    ExprPtr resultExpression;
};

struct ParseError {
    std::string message{};
    std::size_t tokenIndex{0U};
    std::string tokenText{};
    std::string sourcePath{};
};

struct ExpressionParseResult {
    bool success{false};
    FieldScriptAst ast{};
    std::set<std::string> externalVariables;
    std::vector<ParseError> errors;
};

} // namespace yaha
