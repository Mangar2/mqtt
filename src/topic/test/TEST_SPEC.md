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

---

# TEST_SPEC.md — topic/test (TopicMatcher, Module 3.3)

## Tag

`[topic_matcher]`

## Test cases

### Exact matching (3.3.1)

| Name | Scenario | Filter(s) | Publish topic | Expected result count |
|------|----------|-----------|---------------|-----------------------|
| `match_exact_match` | Single exact filter matches | `"sport/tennis"` | `"sport/tennis"` | 1 |
| `match_exact_no_match` | Exact filter does not match different topic | `"sport/tennis"` | `"sport/golf"` | 0 |
| `match_exact_prefix_no_match` | Prefix alone does not match longer topic | `"sport"` | `"sport/tennis"` | 0 |
| `match_empty_trie` | No subscriptions in trie | — | `"sport/tennis"` | 0 |
| `match_returns_correct_subscription` | Returned subscription matches stored data | `"sport"` (QoS1) | `"sport"` | 1, QoS == AtLeastOnce |

### Single-level wildcard `+` (3.3.2)

| Name | Scenario | Filter(s) | Publish topic | Expected result count |
|------|----------|-----------|---------------|-----------------------|
| `match_plus_single_level` | `+` matches one level | `"sport/+/player"` | `"sport/tennis/player"` | 1 |
| `match_plus_multiple_in_filter` | Multiple `+` wildcards | `"+/+/+"` | `"a/b/c"` | 1 |
| `match_plus_root_level` | `+` at root matches any single-level topic | `"+"` | `"sport"` | 1 |
| `match_plus_no_multi_level` | `+` does not match across `/` | `"+"` | `"sport/tennis"` | 0 |

### Multi-level wildcard `#` (3.3.3)

| Name | Scenario | Filter(s) | Publish topic | Expected result count |
|------|----------|-----------|---------------|-----------------------|
| `match_hash_only` | `#` alone matches any topic | `"#"` | `"sport/tennis"` | 1 |
| `match_hash_multi_level` | `sport/#` matches deep topic | `"sport/#"` | `"sport/tennis/wimbledon"` | 1 |
| `match_hash_zero_remaining` | `sport/#` matches topic with no sub-levels | `"sport/#"` | `"sport"` | 1 |
| `match_hash_and_exact` | Both `sport/#` and `sport/tennis` match | `"sport/#"`, `"sport/tennis"` | `"sport/tennis"` | 2 |

### System topic exclusion (3.3.4)

| Name | Scenario | Filter(s) | Publish topic | Expected result count |
|------|----------|-----------|---------------|-----------------------|
| `match_system_topic_excluded_from_hash` | `#` does not match system topic | `"#"` | `"$SYS/info"` | 0 |
| `match_system_topic_excluded_from_plus` | `+/info` does not match system topic | `"+/info"` | `"$SYS/info"` | 0 |
| `match_system_topic_exact` | Exact filter matches system topic | `"$SYS/info"` | `"$SYS/info"` | 1 |
| `match_system_topic_hash_after_prefix` | `$SYS/#` matches system topic | `"$SYS/#"` | `"$SYS/info"` | 1 |
| `match_system_topic_plus_after_prefix` | `$SYS/+` matches system topic | `"$SYS/+"` | `"$SYS/info"` | 1 |

### Multi-client scenarios

| Name | Scenario | Clients / Filters | Publish topic | Expected result count |
|------|----------|-------------------|---------------|-----------------------|
| `match_multiple_clients_same_filter` | Two clients hold the same filter | A:`"sport/#"`, B:`"sport/#"` | `"sport/tennis"` | 2 |
| `match_multiple_overlapping_filters` | Same client has overlapping filters | A:`"#"`, A:`"sport/#"` | `"sport/tennis"` | 2 |
