# json/test — TEST_SPEC

## JsonValue::parse + stringify

1. parse_object_and_stringify_roundtrip
- Scenario: Parse nested object with array and escape sequence.
- Input: `{"name":"yaha","active":true,"list":[1,2,3],"text":"line\\nnext"}`
- Expected: parse succeeds; `stringify()` returns equivalent compact JSON; values are accessible by key/index.

2. parse_invalid_json_returns_nullopt
- Scenario: Parse malformed JSON text via `try_parse`.
- Input: `{ "a": [1, 2 }`
- Expected: returns `std::nullopt`.

3. parse_invalid_json_throws_with_offset
- Scenario: Parse malformed JSON text via throwing API.
- Input: `{ "a" 1 }`
- Expected: throws `JsonException` with `JsonError::UnexpectedToken` and non-zero/valid offset.

## JavaScript-like object and array behavior

4. object_operator_brackets_auto_create
- Scenario: Build object tree using key-based `operator[]`.
- Input: start with null value and assign nested keys.
- Expected: value becomes object; nested keys are created and values readable.

5. array_operator_brackets_auto_growth
- Scenario: Build array using index-based `operator[]` on null value.
- Input: assign index `2`.
- Expected: value becomes array of size `3`; missing entries are null.

6. push_back_on_null_creates_array
- Scenario: Append to null value.
- Input: `push_back("a")`, `push_back("b")`.
- Expected: value becomes array with two entries.

7. invalid_type_access_throws
- Scenario: Access number as string.
- Input: `JsonValue{5.0}.as_string()`
- Expected: throws `JsonException` with `JsonError::InvalidType`.

8. missing_key_throws
- Scenario: Access missing object key with `at`.
- Input: object without requested key.
- Expected: throws `JsonException` with `JsonError::MissingKey`.

9. out_of_range_index_throws
- Scenario: Access array index beyond bounds with `at`.
- Input: array with one entry and index `3`.
- Expected: throws `JsonException` with `JsonError::IndexOutOfRange`.

10. unicode_escape_parsing_supports_surrogate_pair
- Scenario: Parse string with UTF-16 surrogate pair escape.
- Input: `"\uD83D\uDE03"`
- Expected: parse succeeds and resulting UTF-8 string is non-empty.
