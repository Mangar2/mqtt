#include "yaha/automation/expression_parser.h"

#include <cctype>
#include <optional>
#include <stdexcept>
#include <utility>

#include "yaha/automation/expression_tokenizer.h"

namespace yaha {
namespace {

[[nodiscard]] bool isIdentifierStart(const char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

[[nodiscard]] bool isIdentifierChar(const char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

[[nodiscard]] bool isIdentifier(const std::string& token) {
    if (token.empty() || !isIdentifierStart(token.front())) {
        return false;
    }

    for (std::size_t i = 1U; i < token.size(); ++i) {
        if (!isIdentifierChar(token[i])) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool isQuotedString(const std::string& token) {
    if (token.size() < 2U) {
        return false;
    }

    const char quote = token.front();
    return (quote == '\'' || quote == '"') && token.back() == quote;
}

[[nodiscard]] bool parseNumber(const std::string& token, double* value) {
    if (token.empty()) {
        return false;
    }

    std::size_t index = 0U;
    try {
        *value = std::stod(token, &index);
    } catch (...) {
        return false;
    }

    return index == token.size();
}

[[nodiscard]] bool isVariableRef(const std::string& token) {
    if (token.find('/') == std::string::npos || token.empty()) {
        return false;
    }

    const char first = token.front();
    if (!(first == '$' || first == '/' || isIdentifierStart(first))) {
        return false;
    }

    for (const char current : token) {
        if (std::isalnum(static_cast<unsigned char>(current)) != 0) {
            continue;
        }

        if (current == '_' || current == '-' || current == '.' || current == '$'
            || current == '/' || current == ' ') {
            continue;
        }

        return false;
    }

    return true;
}

[[nodiscard]] bool isComparisonOperator(const std::string& token) {
    return token == "=" || token == "!=" || token == "<>"
        || token == ">" || token == "<" || token == ">=" || token == "<=";
}

[[nodiscard]] BinaryOperator toComparisonOperator(const std::string& token) {
    if (token == "=") {
        return BinaryOperator::Eq;
    }
    if (token == "!=" || token == "<>") {
        return BinaryOperator::Neq;
    }
    if (token == ">") {
        return BinaryOperator::Gt;
    }
    if (token == "<") {
        return BinaryOperator::Lt;
    }
    if (token == ">=") {
        return BinaryOperator::Ge;
    }
    return BinaryOperator::Le;
}

[[nodiscard]] ExprPtr makeExpr(ExprNode node) {
    return std::make_shared<ExprNode>(ExprNode{.node = std::move(node.node)});
}

class LineParser {
public:
    explicit LineParser(std::vector<std::string> tokens)
        : tokens_(std::move(tokens)) {
    }

    [[nodiscard]] std::optional<MapDeclarationAst> parseDeclaration(std::vector<ParseError>* errors) {
        if (tokens_.empty()) {
            errors->push_back(ParseError{.message = "empty declaration line"});
            return std::nullopt;
        }

        const std::string name = consume();
        if (!isIdentifier(name)) {
            errors->push_back(ParseError{.message = "declaration must start with identifier", .tokenIndex = 0U, .tokenText = name});
            return std::nullopt;
        }

        if (!match("=")) {
            errors->push_back(ParseError{.message = "declaration requires '=' after identifier", .tokenIndex = index_, .tokenText = peekOrEmpty()});
            return std::nullopt;
        }

        const ExprPtr expr = parseMapLiteral(errors);
        if (!expr) {
            return std::nullopt;
        }

        if (!atEnd()) {
            errors->push_back(ParseError{.message = "unexpected trailing tokens in declaration", .tokenIndex = index_, .tokenText = peekOrEmpty()});
            return std::nullopt;
        }

        if (!std::holds_alternative<MapLiteralNode>(expr->node)) {
            errors->push_back(ParseError{.message = "declaration value must be a map literal"});
            return std::nullopt;
        }

        const auto& literal = std::get<MapLiteralNode>(expr->node);
        return MapDeclarationAst{.name = name, .entries = literal.entries};
    }

    [[nodiscard]] ExprPtr parseExpressionLine(std::vector<ParseError>* errors) {
        const ExprPtr expression = parseExpression(errors);
        if (!expression) {
            return nullptr;
        }

        if (!atEnd()) {
            errors->push_back(ParseError{.message = "unexpected trailing tokens in expression", .tokenIndex = index_, .tokenText = peekOrEmpty()});
            return nullptr;
        }

        return expression;
    }

private:
    [[nodiscard]] ExprPtr parseExpression(std::vector<ParseError>* errors) {
        return parseOr(errors);
    }

    [[nodiscard]] ExprPtr parseOr(std::vector<ParseError>* errors) {
        ExprPtr left = parseAnd(errors);
        if (!left) {
            return nullptr;
        }

        while (match("or")) {
            ExprPtr right = parseAnd(errors);
            if (!right) {
                return nullptr;
            }
            left = makeExpr(ExprNode{.node = BinaryOpNode{.op = BinaryOperator::Or, .left = left, .right = right}});
        }

        return left;
    }

    [[nodiscard]] ExprPtr parseAnd(std::vector<ParseError>* errors) {
        ExprPtr left = parseCompare(errors);
        if (!left) {
            return nullptr;
        }

        while (match("and")) {
            ExprPtr right = parseCompare(errors);
            if (!right) {
                return nullptr;
            }
            left = makeExpr(ExprNode{.node = BinaryOpNode{.op = BinaryOperator::And, .left = left, .right = right}});
        }

        return left;
    }

    [[nodiscard]] ExprPtr parseCompare(std::vector<ParseError>* errors) {
        ExprPtr left = parseAdd(errors);
        if (!left) {
            return nullptr;
        }

        if (!atEnd() && isComparisonOperator(peek())) {
            const std::string opToken = consume();
            ExprPtr right = parseAdd(errors);
            if (!right) {
                return nullptr;
            }
            left = makeExpr(ExprNode{.node = BinaryOpNode{
                .op = toComparisonOperator(opToken),
                .left = left,
                .right = right}});
        }

        return left;
    }

    [[nodiscard]] ExprPtr parseAdd(std::vector<ParseError>* errors) {
        ExprPtr left = parseUnary(errors);
        if (!left) {
            return nullptr;
        }

        while (!atEnd() && (peek() == "+" || peek() == "-")) {
            const std::string opToken = consume();
            ExprPtr right = parseUnary(errors);
            if (!right) {
                return nullptr;
            }
            left = makeExpr(ExprNode{.node = BinaryOpNode{
                .op = (opToken == "+") ? BinaryOperator::Add : BinaryOperator::Sub,
                .left = left,
                .right = right}});
        }

        return left;
    }

    [[nodiscard]] ExprPtr parseUnary(std::vector<ParseError>* errors) {
        if (match("not")) {
            ExprPtr operand = parsePrimary(errors);
            if (!operand) {
                return nullptr;
            }
            return makeExpr(ExprNode{.node = UnaryOpNode{.op = UnaryOperator::Not, .operand = operand}});
        }

        return parsePrimary(errors);
    }

    [[nodiscard]] bool looksLikeMapLiteral() const {
        if (atEnd() || peek() != "(") {
            return false;
        }

        std::size_t depth = 0U;
        for (std::size_t i = index_; i < tokens_.size(); ++i) {
            if (tokens_[i] == "(") {
                depth += 1U;
                continue;
            }
            if (tokens_[i] == ")") {
                if (depth == 0U) {
                    return false;
                }
                depth -= 1U;
                if (depth == 0U) {
                    return false;
                }
                continue;
            }
            if (tokens_[i] == ":" && depth == 1U) {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] ExprPtr parsePrimary(std::vector<ParseError>* errors) {
        if (atEnd()) {
            errors->push_back(ParseError{.message = "unexpected end of expression", .tokenIndex = index_});
            return nullptr;
        }

        if (peek() == "if") {
            return parseIfCall(errors);
        }

        if (peek() == "(") {
            if (looksLikeMapLiteral()) {
                return parseMapLiteral(errors);
            }

            consume();
            const ExprPtr expression = parseExpression(errors);
            if (!expression) {
                return nullptr;
            }
            if (!match(")")) {
                errors->push_back(ParseError{.message = "missing ')'", .tokenIndex = index_, .tokenText = peekOrEmpty()});
                return nullptr;
            }
            return expression;
        }

        if (index_ + 1U < tokens_.size() && isIdentifier(peek()) && tokens_[index_ + 1U] == "(") {
            return parseMapCall(errors);
        }

        const std::string token = consume();

        if (isQuotedString(token)) {
            return makeExpr(ExprNode{.node = LiteralNode{.value = token.substr(1U, token.size() - 2U)}});
        }

        double numericValue = 0.0;
        if (parseNumber(token, &numericValue)) {
            return makeExpr(ExprNode{.node = LiteralNode{.value = numericValue}});
        }

        if (isVariableRef(token)) {
            return makeExpr(ExprNode{.node = VariableRefNode{.name = token}});
        }

        if (isIdentifier(token)) {
            return makeExpr(ExprNode{.node = IdentifierNode{.name = token}});
        }

        errors->push_back(ParseError{.message = "invalid token in expression", .tokenIndex = index_ - 1U, .tokenText = token});
        return nullptr;
    }

    [[nodiscard]] ExprPtr parseIfCall(std::vector<ParseError>* errors) {
        consume();
        if (!match("(")) {
            errors->push_back(ParseError{.message = "if call requires '('", .tokenIndex = index_, .tokenText = peekOrEmpty()});
            return nullptr;
        }

        const ExprPtr condition = parseExpression(errors);
        if (!condition) {
            return nullptr;
        }

        if (!match(",")) {
            errors->push_back(ParseError{.message = "if call requires ',' after condition", .tokenIndex = index_, .tokenText = peekOrEmpty()});
            return nullptr;
        }

        const ExprPtr trueValue = parseExpression(errors);
        if (!trueValue) {
            return nullptr;
        }

        if (!match(",")) {
            errors->push_back(ParseError{.message = "if call requires second ','", .tokenIndex = index_, .tokenText = peekOrEmpty()});
            return nullptr;
        }

        const ExprPtr falseValue = parseExpression(errors);
        if (!falseValue) {
            return nullptr;
        }

        if (!match(")")) {
            errors->push_back(ParseError{.message = "if call requires closing ')'", .tokenIndex = index_, .tokenText = peekOrEmpty()});
            return nullptr;
        }

        return makeExpr(ExprNode{.node = IfCallNode{.condition = condition, .trueValue = trueValue, .falseValue = falseValue}});
    }

    [[nodiscard]] ExprPtr parseMapCall(std::vector<ParseError>* errors) {
        const std::string name = consume();
        consume();

        const ExprPtr selector = parseExpression(errors);
        if (!selector) {
            return nullptr;
        }

        if (!match(")")) {
            errors->push_back(ParseError{.message = "map call requires closing ')'", .tokenIndex = index_, .tokenText = peekOrEmpty()});
            return nullptr;
        }

        return makeExpr(ExprNode{.node = MapCallNode{.name = name, .selector = selector}});
    }

    [[nodiscard]] std::optional<MapKeyAst> parseMapKey(std::vector<ParseError>* errors) {
        if (atEnd()) {
            errors->push_back(ParseError{.message = "unexpected end while parsing map key", .tokenIndex = index_});
            return std::nullopt;
        }

        const std::string token = consume();
        if (token == "default") {
            return MapKeyAst{.isDefault = true, .token = token};
        }

        if (isQuotedString(token) || isVariableRef(token) || isIdentifier(token)) {
            return MapKeyAst{.isDefault = false, .token = token};
        }

        double value = 0.0;
        if (parseNumber(token, &value)) {
            return MapKeyAst{.isDefault = false, .token = token};
        }

        errors->push_back(ParseError{.message = "invalid map key token", .tokenIndex = index_ - 1U, .tokenText = token});
        return std::nullopt;
    }

    [[nodiscard]] ExprPtr parseMapLiteral(std::vector<ParseError>* errors) {
        if (!match("(")) {
            errors->push_back(ParseError{.message = "map literal must start with '('", .tokenIndex = index_, .tokenText = peekOrEmpty()});
            return nullptr;
        }

        std::vector<MapEntryAst> entries;

        while (true) {
            const std::optional<MapKeyAst> key = parseMapKey(errors);
            if (!key.has_value()) {
                return nullptr;
            }

            if (!match(":")) {
                errors->push_back(ParseError{.message = "map entry requires ':'", .tokenIndex = index_, .tokenText = peekOrEmpty()});
                return nullptr;
            }

            const ExprPtr value = parseExpression(errors);
            if (!value) {
                return nullptr;
            }

            entries.push_back(MapEntryAst{.key = *key, .value = value});

            if (match(")")) {
                break;
            }
            if (!match(",")) {
                errors->push_back(ParseError{.message = "map literal requires ',' or ')'", .tokenIndex = index_, .tokenText = peekOrEmpty()});
                return nullptr;
            }
        }

        if (entries.empty()) {
            errors->push_back(ParseError{.message = "map literal must contain at least one entry"});
            return nullptr;
        }

        return makeExpr(ExprNode{.node = MapLiteralNode{.entries = std::move(entries)}});
    }

    [[nodiscard]] bool atEnd() const noexcept {
        return index_ >= tokens_.size();
    }

    [[nodiscard]] const std::string& peek() const {
        return tokens_[index_];
    }

    [[nodiscard]] std::string peekOrEmpty() const {
        if (atEnd()) {
            return {};
        }
        return tokens_[index_];
    }

    std::string consume() {
        const std::string token = tokens_[index_];
        index_ += 1U;
        return token;
    }

    [[nodiscard]] bool match(const std::string& token) {
        if (atEnd() || tokens_[index_] != token) {
            return false;
        }
        index_ += 1U;
        return true;
    }

    std::vector<std::string> tokens_;
    std::size_t index_{0U};
};

void collectExternalVariablesFromExpr(const ExprPtr& expression, std::set<std::string>* externalVariables) {
    if (!expression) {
        return;
    }

    if (std::holds_alternative<VariableRefNode>(expression->node)) {
        const auto& variable = std::get<VariableRefNode>(expression->node).name;
        if (!variable.empty() && variable.front() != '/') {
            externalVariables->insert(variable);
        }
        return;
    }

    if (std::holds_alternative<UnaryOpNode>(expression->node)) {
        collectExternalVariablesFromExpr(std::get<UnaryOpNode>(expression->node).operand, externalVariables);
        return;
    }

    if (std::holds_alternative<BinaryOpNode>(expression->node)) {
        const auto& node = std::get<BinaryOpNode>(expression->node);
        collectExternalVariablesFromExpr(node.left, externalVariables);
        collectExternalVariablesFromExpr(node.right, externalVariables);
        return;
    }

    if (std::holds_alternative<IfCallNode>(expression->node)) {
        const auto& node = std::get<IfCallNode>(expression->node);
        collectExternalVariablesFromExpr(node.condition, externalVariables);
        collectExternalVariablesFromExpr(node.trueValue, externalVariables);
        collectExternalVariablesFromExpr(node.falseValue, externalVariables);
        return;
    }

    if (std::holds_alternative<MapCallNode>(expression->node)) {
        collectExternalVariablesFromExpr(std::get<MapCallNode>(expression->node).selector, externalVariables);
        return;
    }

    if (std::holds_alternative<MapLiteralNode>(expression->node)) {
        const auto& node = std::get<MapLiteralNode>(expression->node);
        for (const auto& entry : node.entries) {
            collectExternalVariablesFromExpr(entry.value, externalVariables);
        }
    }
}

[[nodiscard]] std::vector<std::vector<std::string>> splitIntoLines(const std::vector<std::string>& tokens) {
    std::vector<std::vector<std::string>> lines;
    lines.emplace_back();

    for (const auto& token : tokens) {
        if (token == "\\n") {
            lines.emplace_back();
            continue;
        }
        lines.back().push_back(token);
    }

    std::vector<std::vector<std::string>> filtered;
    for (auto& line : lines) {
        if (!line.empty()) {
            filtered.push_back(std::move(line));
        }
    }

    return filtered;
}

} // namespace

ExpressionParseResult ExpressionParser::parse(const std::string& script) {
    ExpressionParseResult result;

    const std::vector<std::string> tokens = ExpressionTokenizer::tokenize(script);
    const std::vector<std::vector<std::string>> lines = splitIntoLines(tokens);

    if (lines.empty()) {
        result.errors.push_back(ParseError{.message = "expression script is empty"});
        return result;
    }

    for (std::size_t index = 0U; index + 1U < lines.size(); ++index) {
        LineParser parser{lines[index]};
        const std::optional<MapDeclarationAst> declaration = parser.parseDeclaration(&result.errors);
        if (!declaration.has_value()) {
            return result;
        }
        result.ast.declarations.push_back(*declaration);
    }

    LineParser parser{lines.back()};
    result.ast.resultExpression = parser.parseExpressionLine(&result.errors);
    if (!result.ast.resultExpression) {
        return result;
    }

    for (const auto& declaration : result.ast.declarations) {
        for (const auto& entry : declaration.entries) {
            collectExternalVariablesFromExpr(entry.value, &result.externalVariables);
        }
    }
    collectExternalVariablesFromExpr(result.ast.resultExpression, &result.externalVariables);

    result.success = true;
    return result;
}

} // namespace yaha
