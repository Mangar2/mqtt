# helper/test — TEST_SPEC

## mqtt::helper::toLower

1. tolower_mixed_case_returns_lowercase
- Scenario: Convert mixed-case ASCII text.
- Input: `"HeLLo World_42"`
- Expected: `"hello world_42"`.

2. tolower_empty_string_returns_empty
- Scenario: Convert empty text.
- Input: `""`
- Expected: `""`.

3. tolower_already_lowercase_is_unchanged
- Scenario: Convert text that is already lowercase.
- Input: `"already lower"`
- Expected: `"already lower"`.

## mqtt::helper::trim

4. trim_removes_leading_and_trailing_whitespace
- Scenario: Trim text with spaces, tabs, and newlines on both sides.
- Input: `" \t\n hello world \r\n"`
- Expected: `"hello world"`.

5. trim_all_whitespace_returns_empty
- Scenario: Trim text consisting only of whitespace.
- Input: `"   \t  "`
- Expected: `""`.

6. trim_no_whitespace_is_unchanged
- Scenario: Trim text without any leading/trailing whitespace.
- Input: `"clean"`
- Expected: `"clean"`.

7. trim_empty_string_returns_empty
- Scenario: Trim empty text.
- Input: `""`
- Expected: `""`.

## mqtt::helper::split

8. split_basic_delimiter_trims_each_token
- Scenario: Split comma-separated text with surrounding whitespace per token.
- Input: `"a, b ,c"`, delimiter `','`
- Expected: `{"a", "b", "c"}`.

9. split_consecutive_delimiters_yield_empty_token
- Scenario: Split text with two consecutive delimiters.
- Input: `"a,,b"`, delimiter `','`
- Expected: `{"a", "", "b"}`.

10. split_trailing_delimiter_has_no_trailing_empty_token
- Scenario: Split text ending with the delimiter.
- Input: `"a,b,"`, delimiter `','`
- Expected: `{"a", "b"}`.

11. split_empty_string_returns_empty_vector
- Scenario: Split empty text.
- Input: `""`, delimiter `','`
- Expected: `{}`.

12. split_no_delimiter_present_returns_single_token
- Scenario: Split text that does not contain the delimiter.
- Input: `"single"`, delimiter `','`
- Expected: `{"single"}`.
