# automation — YAHA Automation Rules Engine

## Purpose

Implements YAHA automation domain logic defined in `spec/yaha/SPEC-automation.md`.
This module evaluates rule DSL expressions and emits outbound MQTT messages.

Current implementation step:
- Expression tokenizer for the Python-style DSL.
- Internal variable calculator including sunrise/sunset and twilight calculations.

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
| `test/TEST_SPEC.md` | Unit-test specification |
| `test/expression_tokenizer_test.cpp` | Catch2 unit tests |
| `test/internal_variables_test.cpp` | Catch2 unit tests for internal variable computation |
