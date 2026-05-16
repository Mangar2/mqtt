# automation tokenizer test specification

## Scope

Unit tests for the first automation step: lexical tokenization of expression DSL scripts.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `expression_tokenizer_splits_declaration_and_expression` | multi-line declaration + final expression | `presence=(1:awake,default:absent)\n$MONITOR/presence!=presence($MONITOR/presence/set)` | vector preserves declaration/expression tokens and `"\\n"` separator |
| `expression_tokenizer_keeps_quoted_literals_as_single_token` | quoted strings with spaces remain one token | `if('sleep mode' = mode, "on", off)` | quoted values remain single tokens including quotes |
| `expression_tokenizer_recognizes_comparators` | all comparison operators tokenized correctly | `a=b and c!=d and e<>f and g>=h and i<=j and k>l and m<n` | vector contains exact operator tokens |
| `expression_tokenizer_keeps_variable_reference_with_spaces` | unquoted slash variable with embedded spaces | `$MONITOR/room/motion sensor/detection state = on` | topic-like variable stays one token until comparator |
| `expression_tokenizer_normalizes_crlf_to_newline_token` | CRLF line ending support | `a=b\r\nc=d` | one `"\\n"` token between lines |
| `expression_tokenizer_rejects_unterminated_quote` | invalid string literal handling | `'abc` | throws invalid_argument |
| `expression_tokenizer_rejects_lone_exclamation_mark` | invalid token handling | `a ! b` | throws invalid_argument |
| `expression_tokenizer_keeps_negative_number_literal` | numeric literal with unary minus | `if(a = b, -12.5, 3)` | `-12.5` appears as one number token |
| `internal_variables_returns_all_required_keys` | full built-in variable set generation | date + longitude/latitude | map contains all ten required internal keys |
| `internal_variables_weekday_uses_sunday_zero_index` | weekday encoding parity | date for Sunday | `/weekday` is numeric `0` |
| `internal_variables_sun_times_have_expected_order` | twilight/sun order sanity | summer date + coordinates | dawn chain <= sunrise < sunset <= dusk chain |
| `internal_variables_time_value_matches_input_date` | passthrough current time variable | explicit date-time input | `/time` equals input time point |
| `expression_parser_parses_declarations_and_collects_external_topics` | declaration + result script parse | multiline script with map declaration and topic references | parser succeeds, declaration exists, external variable set contains referenced topics |
| `expression_parser_builds_operator_precedence_tree` | precedence correctness for or/and/not/comparison | `a = b and c = d or not e = f` | top AST operator is `or` |
| `rules_tree_parser_exposes_snippets_by_slash_path` | structured tree traversal and snippet addressing | nested object with `check` and `value` strings | snippets available under slash paths and external topics aggregated |
| `rules_tree_parser_reports_path_on_expression_error` | path-aware parser error reporting | nested object with invalid `if` call argument count | parse error includes exact snippet source path |
| `rules_tree_parser_parses_rules_json_without_errors` | end-to-end parser validation against converted fixture file | `test/rules.json` | json reader and rules parser both succeed with zero parse errors |
| `rules_tree_parser_collects_external_topics_from_rules_json` | external topic collection from full rules tree | `test/rules.json` | external variable set is non-empty and contains known topics |
| `expression_evaluator_evaluates_map_declaration_and_call` | declaration map invocation runtime | parsed script with declaration and map call plus variable map | returns mapped string value and reports used variable |
| `expression_evaluator_evaluates_if_expression` | conditional expression runtime | parsed `if` script and variable context | returns selected branch value |
| `expression_evaluator_supports_time_arithmetic_in_minutes` | time +/- numeric minute semantics | comparison script with `/time` and `/sunrise` | evaluates to true with minute-shifted comparison |
| `expression_evaluator_uses_local_time_for_timepoint_comparisons_and_reason` | local-time handling for timepoint comparisons and explain-reason formatting | compare `"/time"` against `"00:00"` with `/time` as runtime `system_clock::time_point` | evaluation uses local wall-clock hour/min/sec text in reason and comparison |
| `expression_evaluator_handles_undefined_variable_like_legacy` | missing variable handling with legacy parity | script with external variable and empty variable map | evaluation succeeds, final value is false, no errors, and reason lists undefined variable |
| `expression_evaluator_can_evaluate_program_from_rules_fixture` | fixture-like expression runtime | expression shape from converted `test/rules.json` | returns expected branch output |
| `single_rule_processor_processes_complete_rule_and_emits_message` | full rule handling with check/value/qos | rule object with topic check and value expression plus variable map | triggered result contains outbound message with expected topic value qos and used variables |
| `single_rule_processor_returns_no_message_when_check_is_false` | complete rule where condition is false | rule object with false check and value | success with triggered=false and no outbound message |
| `single_rule_processor_reports_invalid_rule_structure` | required field validation | rule object without topic field | processing fails with structure error |
| `single_rule_processor_reports_expression_errors` | expression parse validation in check field | rule object with invalid check expression | processing fails with parse/evaluation errors |
| `expression_evaluator_provides_human_readable_reason` | mini-program style reason text for expression evaluation | `if($SYS/presence = awake, on, off)` with `$SYS/presence=awake` | success and non-empty reason containing variable/value comparison and chosen branch |
| `single_rule_processor_trace_includes_reasoned_check_and_value` | trace output contains human-readable check/value reasons without variable snapshots | rule with check and value expressions and matching variable context | trace contains `check reason=` and `value reason=` entries and no `var ...=...` entries |
| `single_rule_processor_adds_trace_reason_to_emitted_message_when_program_executed` | rule output message contains one human-readable summary reason when check/value programs run | triggered rule with check and value expression programs | emitted message has exactly one reason entry starting with `Rule:` and containing check/value human-readable text |
| `single_rule_processor_uses_explicit_rule_identifier_in_summary_reason` | explicit rule identifier overrides output topic in summary reason | triggered rule with check/value expression programs and explicit rule identifier `rules/presenceOn` | emitted message reason starts with `Rule: presenceOn` |
| `single_rule_processor_keeps_reason_empty_for_literal_rule_without_program` | literal-only rule does not force synthetic trace reason | triggered rule with numeric literal value and no check program | emitted message reason remains empty |
| `rules_tree_processor_processes_all_rules_in_tree` | end-to-end processing over complete rules tree | root tree containing two rule objects and runtime variables | processed/triggered counters and emitted messages match expected outcomes |
| `rules_tree_processor_collects_path_aware_errors_and_keeps_valid_results` | mixed valid and invalid rules in one tree | root tree with one valid and one invalid rule object | returns error list with path context while preserving message from valid rule |
| `rules_tree_processor_emitted_messages_include_evaluation_trace_reason` | automation tree output propagates single human-readable summary reason to message | rule tree with one triggered expression-based rule | emitted message reason has exactly one entry starting with `Rule:` |
| `rules_tree_processor_skips_rules_with_active_false` | rule with active:false is never processed | tree with one active rule and one inactive rule (active:false) | processedRules=1, only active rule message emitted, inactive rule has no output |
