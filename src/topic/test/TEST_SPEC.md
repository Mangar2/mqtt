# TEST_SPEC.md — topic/test

Unit tests for Module 3.1: Topic Validator.

## Tag

`[topic_validator]`

## Test cases

### validate_topic_name

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `topic_name_valid_simple` | Plain ASCII topic | `"sport/tennis/player1"` | No exception |
| `topic_name_valid_single_char` | Single character | `"a"` | No exception |
| `topic_name_valid_single_slash` | Single slash | `"/"` | No exception |
| `topic_name_valid_max_length` | Exactly 65535 bytes | string of 65535 'a' chars | No exception |
| `topic_name_empty` | Empty string | `""` | Throws `TopicError::EmptyTopic` |
| `topic_name_too_long` | 65536 bytes | string of 65536 'a' chars | Throws `TopicError::TopicTooLong` |
| `topic_name_null_char` | Contains U+0000 | `"sport\0tennis"` | Throws `TopicError::NullCharacter` |
| `topic_name_wildcard_hash` | Contains '#' | `"sport/#"` | Throws `TopicError::WildcardInTopicName` |
| `topic_name_wildcard_plus` | Contains '+' | `"sport/+/player"` | Throws `TopicError::WildcardInTopicName` |
| `topic_name_hash_only` | '#' alone | `"#"` | Throws `TopicError::WildcardInTopicName` |
| `topic_name_plus_only` | '+' alone | `"+"` | Throws `TopicError::WildcardInTopicName` |
| `topic_name_system_topic` | '$' prefix is valid topic name | `"$SYS/info"` | No exception |

### validate_topic_filter

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `topic_filter_valid_no_wildcards` | Plain filter | `"sport/tennis"` | No exception |
| `topic_filter_valid_hash_only` | '#' alone | `"#"` | No exception |
| `topic_filter_valid_hash_at_end` | '#' at end after separator | `"sport/#"` | No exception |
| `topic_filter_valid_plus_single_level` | '+' alone | `"+"` | No exception |
| `topic_filter_valid_plus_start` | '+' at start | `"+/tennis"` | No exception |
| `topic_filter_valid_plus_end` | '+' at end | `"sport/+"` | No exception |
| `topic_filter_valid_plus_middle` | '+' in middle | `"sport/+/player"` | No exception |
| `topic_filter_valid_multiple_plus` | Multiple '+' levels | `"+/+/+"` | No exception |
| `topic_filter_valid_plus_and_hash` | '+' and '#' combined | `"sport/+/#"` | No exception |
| `topic_filter_valid_max_length` | Exactly 65535 bytes | string of 65535 'a' chars | No exception |
| `topic_filter_empty` | Empty string | `""` | Throws `TopicError::EmptyTopic` |
| `topic_filter_too_long` | 65536 bytes | string of 65536 'a' chars | Throws `TopicError::TopicTooLong` |
| `topic_filter_null_char` | Contains U+0000 | `"sport\0tennis"` | Throws `TopicError::NullCharacter` |
| `topic_filter_hash_not_last` | '#' not at end | `"sport/#/player"` | Throws `TopicError::InvalidWildcard` |
| `topic_filter_hash_without_sep` | '#' without preceding separator | `"sport#"` | Throws `TopicError::InvalidWildcard` |
| `topic_filter_plus_not_full_level_start` | '+' not at level boundary | `"sport+/player"` | Throws `TopicError::InvalidWildcard` |
| `topic_filter_plus_not_full_level_end` | '+' not at trailing boundary | `"sport/play+er"` | Throws `TopicError::InvalidWildcard` |
| `topic_filter_plus_embedded` | '+' embedded in level | `"sp+rt"` | Throws `TopicError::InvalidWildcard` |
| `topic_filter_hash_middle_no_sep` | '#' in middle without separator | `"sport#player"` | Throws `TopicError::InvalidWildcard` |

### is_system_topic

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `system_topic_dollar_prefix` | Starts with '$' | `"$SYS/info"` | `true` |
| `system_topic_dollar_only` | '$' alone | `"$"` | `true` |
| `system_topic_normal` | Normal topic | `"sport/tennis"` | `false` |
| `system_topic_empty` | Empty string | `""` | `false` |
| `system_topic_slash_prefix` | Starts with '/' | `"/sport"` | `false` |
