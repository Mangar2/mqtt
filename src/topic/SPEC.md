# topic — Topic Engine (Module 3)

Validation and matching of MQTT topic names and filters.
Depends on: `data_model/` (Module 1).

## Sub-modules

| Directory / File       | Plan ref | Contents |
|------------------------|----------|----------|
| `topic/topic_error.h`  | 3.1      | `TopicError` enum and `TopicException`. |
| `topic/topic_validator.h/.cpp` | 3.1 | Topic-name and topic-filter validation, system-topic detection. |
| `topic/trie/`          | 3.2      | `SubscriptionTrie` — trie storage for MQTT subscriptions. |

## Module 3.1 — Topic Validator

### Purpose

Validates MQTT 5.0 topic names and topic filters per spec Sections 4.7 and 4.7.3,
and detects system topics (dollar-sign prefix, Section 4.7.2).

### Public API

```cpp
namespace mqtt {

// Validate a publish topic name. Throws TopicException on violation.
void validate_topic_name(std::string_view topic);

// Validate a subscription topic filter. Throws TopicException on violation.
void validate_topic_filter(std::string_view filter);

// Returns true if topic begins with '$' (system topic).
[[nodiscard]] bool is_system_topic(std::string_view topic) noexcept;

} // namespace mqtt
```

### Behaviour

#### `validate_topic_name`

Enforces MQTT spec Section 4.7.1 and Section 4.7.3:

| Rule | Detail |
|------|--------|
| Non-empty | Topic name must be at least 1 byte. |
| Max length | Total UTF-8 byte length must not exceed 65 535 (`Utf8String::k_max_byte_length`). |
| No null character | U+0000 is forbidden (Section 1.5.4). |
| No wildcards | '+' (U+002B) and '#' (U+0023) are forbidden in topic names. |

Throws `TopicException(TopicError::EmptyTopic)` when length == 0.
Throws `TopicException(TopicError::TopicTooLong)` when length > 65535.
Throws `TopicException(TopicError::NullCharacter)` when U+0000 is present.
Throws `TopicException(TopicError::WildcardInTopicName)` when '+' or '#' is present.

#### `validate_topic_filter`

Enforces MQTT spec Section 4.7.1 and Section 4.7.1.2–4.7.1.3:

| Rule | Detail |
|------|--------|
| Non-empty | Filter must be at least 1 byte. |
| Max length | Total UTF-8 byte length must not exceed 65 535. |
| No null character | U+0000 is forbidden. |
| `#` placement | '#' must either be the only character, or immediately follow '/' as the last character (e.g. `sport/#`). |
| `+` placement | '+' must occupy an entire topic level: preceded by '/' or start-of-string, and followed by '/' or end-of-string (e.g. `sport/+/player`). |

Throws `TopicException(TopicError::EmptyTopic)` when length == 0.
Throws `TopicException(TopicError::TopicTooLong)` when length > 65535.
Throws `TopicException(TopicError::NullCharacter)` when U+0000 is present.
Throws `TopicException(TopicError::InvalidWildcard)` for any wildcard position violation.

#### `is_system_topic`

Returns `true` when the first byte of `topic` is `'$'`.
Returns `false` for empty strings.
Does not throw.

### Error codes — `TopicError`

| Enumerator | Meaning |
|------------|---------|
| `EmptyTopic` | Zero-length topic or filter. |
| `TopicTooLong` | Byte length exceeds 65 535. |
| `NullCharacter` | U+0000 present in topic or filter. |
| `WildcardInTopicName` | '+' or '#' used in a publish topic name. |
| `InvalidWildcard` | Wildcard appears in an illegal position within a filter. |

### Constraints

- No dynamic allocation in the hot path.
- All functions operate on `std::string_view` — no ownership semantics.
- `is_system_topic` is `noexcept`.
