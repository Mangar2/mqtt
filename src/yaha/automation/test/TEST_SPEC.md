# automation tokenizer test specification

## Scope

Unit tests for the first automation step: lexical tokenization of expression DSL scripts.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `expression_tokenizer_splits_declaration_and_expression` | multi-line declaration + final expression | `presence=(1:awake,default:absent)\n$MONITORING/presence!=presence($MONITORING/presence/set)` | vector preserves declaration/expression tokens and `"\\n"` separator |
| `expression_tokenizer_keeps_quoted_literals_as_single_token` | quoted strings with spaces remain one token | `if('sleep mode' = mode, "on", off)` | quoted values remain single tokens including quotes |
| `expression_tokenizer_recognizes_comparators` | all comparison operators tokenized correctly | `a=b and c!=d and e<>f and g>=h and i<=j and k>l and m<n` | vector contains exact operator tokens |
| `expression_tokenizer_keeps_variable_reference_with_spaces` | unquoted slash variable with embedded spaces | `$MONITORING/room/motion sensor/detection state = on` | topic-like variable stays one token until comparator |
| `expression_tokenizer_normalizes_crlf_to_newline_token` | CRLF line ending support | `a=b\r\nc=d` | one `"\\n"` token between lines |
| `expression_tokenizer_rejects_unterminated_quote` | invalid string literal handling | `'abc` | throws invalid_argument |
| `expression_tokenizer_rejects_lone_exclamation_mark` | invalid token handling | `a ! b` | throws invalid_argument |
| `expression_tokenizer_keeps_negative_number_literal` | numeric literal with unary minus | `if(a = b, -12.5, 3)` | `-12.5` appears as one number token |
| `internal_variables_returns_all_required_keys` | full built-in variable set generation | date + longitude/latitude | map contains all ten required internal keys |
| `internal_variables_weekday_uses_sunday_zero_index` | weekday encoding parity | date for Sunday | `/weekday` is numeric `0` |
| `internal_variables_sun_times_have_expected_order` | twilight/sun order sanity | summer date + coordinates | dawn chain <= sunrise < sunset <= dusk chain |
| `internal_variables_time_value_matches_input_date` | passthrough current time variable | explicit date-time input | `/time` equals input time point |
