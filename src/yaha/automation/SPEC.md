# automation — YAHA Automation Rules Engine

## Purpose

Implements YAHA automation domain logic defined in `spec/yaha/SPEC-automation.md`.
This module evaluates rule DSL expressions and emits outbound MQTT messages.

Current implementation step:
- Expression tokenizer for the Python-style DSL.
- Internal variable calculator including sunrise/sunset and twilight calculations.
- Expression AST parser and structured rules-tree snippet parser.

## Public API

### Class `ExpressionTokenizer`

| Member | Signature | Notes |
|--------|-----------|-------|
| `tokenize(program)` | `static vector<string>(const string&)` | Splits script source into lexical token strings |

Tokenization behavior:
- Input: one long script string, potentially multi-line.
- Output: ordered token vector where each token is a `std::string`.
- Whitespace ` ` and `\t` is ignored.
- Line breaks are preserved as explicit token `"\n"`.
- Recognized operators and separators:
  - `(` `)` `,` `:` `+` `-`
  - `=` `!=` `<>` `>` `<` `>=` `<=`
- Quoted strings with `'` or `"` are emitted as single tokens including quote chars.
- Bare tokens (identifiers, variable references, numbers, keywords) are emitted as contiguous text chunks.
- Unquoted slash-based variable references may include embedded spaces and remain one token.
- Negative numeric literals are emitted as one token (for example `-12.5`).

Error behavior:
- Unterminated quoted string throws `std::invalid_argument`.
- Lone `!` throws `std::invalid_argument`.

### Expression AST parser

Class:
- `ExpressionParser`

Public contract:
- `parse(script) -> ExpressionParseResult`

`ExpressionParseResult` contains:
- `success` flag
- `FieldScriptAst ast`
- `externalVariables` set containing all external variable references (topics)
- `errors` list with structured parser errors

AST model supports:
- declarations (`name = (key: value, default: value)`)
- literals, identifiers, variable refs
- unary operator: `not`
- binary operators: `+`, `-`, `=`, `!=`, `<>`, `>`, `<`, `>=`, `<=`, `and`, `or`
- calls: `if(...)`, `mapName(selector)`
- inline map literals

External variable extraction rule:
- Every parsed variable reference containing `/` and not starting with `/` is exported as external topic dependency.

### Structured rules-tree parser

Classes/Types:
- `RuleTreeNode` generic structured input node (object/array/string/number/bool)
- `RulesTreeParser`
- `RulesTreeParseResult`

Public contract:
- `RulesTreeParser::parse(root) -> RulesTreeParseResult`

Behavior:
- Traverses full structured tree recursively.
- Parses expression-bearing string fields: `check`, `value`, `time`.
- Stores each parsed snippet under slash path key, for example:
  - `motion/rules/setReceived/check`
- Aggregates external topic variables across all snippets.
- Reports parser errors with `sourcePath` context to identify failing snippet.

### JSON reader for parser input

Classes/Types:
- `RulesTreeJsonReader`
- `RuleTreeJsonReadResult`

Public contract:
- `parseJsonText(jsonText) -> RuleTreeJsonReadResult`
- `parseJsonFile(filePath) -> RuleTreeJsonReadResult`

Behavior:
- Parses JSON object/array/string/number/bool/null into `RuleTreeNode` tree.
- Returns structured read errors including line/column position when parsing fails.
- Intended to validate full fixture files (for example `test/rules.json`) through `RulesTreeParser`.

### Class `InternalVariables`

| Member | Signature | Notes |
|--------|-----------|-------|
| Constructor | `InternalVariables(double longitude, double latitude)` | Stores geo coordinates in decimal degrees |
| `calculate(date)` | `VariableMap(const TimePoint&)` | Returns filled map for all built-in internal variables |

Value model:
- `TimePoint = std::chrono::system_clock::time_point`
- `Value = std::variant<TimePoint, double>`
- `VariableMap = std::map<std::string, Value>`

Produced keys:
- `/time`
- `/weekday` (`0=Sun ... 6=Sat`)
- `/sunrise`, `/sunset`
- `/civildawn`, `/civildusk`
- `/nauticaldawn`, `/nauticaldusk`
- `/astronomicaldawn`, `/astronomicaldusk`

Sun-time calculation behavior:
- Uses integrated astronomical computation in this module.
- Inputs are evaluation date (UTC day context), longitude, latitude, and zenith angle.
- Zenith values:
  - sunrise/sunset: `90.833`
  - civil: `96`
  - nautical: `102`
  - astronomical: `108`

Error behavior:
- Throws `std::runtime_error` when a sun event is undefined for date/coordinates.

## Files

| File | Role |
|------|------|
| `expression_tokenizer.h` | Tokenizer declaration |
| `expression_tokenizer.cpp` | Tokenizer implementation |
| `internal_variables.h` | Internal variable calculator declaration |
| `internal_variables.cpp` | Internal variable calculator implementation |
| `expression_ast.h` | AST and parser result/error type declarations |
| `expression_parser.h` | Single-script expression parser declaration |
| `expression_parser.cpp` | Single-script expression parser implementation |
| `rules_tree_parser.h` | Structured tree parser declaration and input node model |
| `rules_tree_parser.cpp` | Structured tree traversal and snippet parsing implementation |
| `rules_tree_json_reader.h` | JSON-to-RuleTreeNode reader declaration |
| `rules_tree_json_reader.cpp` | JSON-to-RuleTreeNode reader implementation |
| `test/TEST_SPEC.md` | Unit-test specification |
| `test/expression_tokenizer_test.cpp` | Catch2 unit tests |
| `test/internal_variables_test.cpp` | Catch2 unit tests for internal variable computation |
| `test/expression_parser_test.cpp` | Catch2 unit tests for AST parser and tree parser |
| `test/rules_tree_parser_rules_json_test.cpp` | Catch2 integration tests against `test/rules.json` |
