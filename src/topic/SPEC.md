# topic — Topic Engine (Module 3)

Validation and matching of MQTT topic names and filters.
Depends on: `data_model/` (Module 1).

## Sub-modules

| Directory / File       | Plan ref | Contents |
|------------------------|----------|----------|
| `topic/topic_error.h`  | 3.1      | `TopicError` enum and `TopicException`. |
| `topic/topic_validator.h/.cpp` | 3.1 | Topic-name and topic-filter validation, system-topic detection. |
| `topic/trie/`          | 3.2      | `SubscriptionTrie` — trie storage for MQTT subscriptions. |
| `topic/topic_matcher.h/.cpp`   | 3.3 | `TopicMatcher` — topic-name matching against a `SubscriptionTrie`. |

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

---

## Module 3.3 — TopicMatcher

### Purpose

Matches a publish topic name against all subscriptions stored in a
`SubscriptionTrie` and returns every `(client_id, Subscription)` pair whose
topic filter matches the topic name, following MQTT 5.0 Section 4.7.

### Public API

```cpp
namespace mqtt {

struct MatchResult {
    std::string  client_id;    // Subscribing client identifier.
    Subscription subscription; // The matching subscription.
};

class TopicMatcher {
public:
    // Find all subscriptions in trie that match topic_name.
    [[nodiscard]] static std::vector<MatchResult>
    match(const SubscriptionTrie& trie, std::string_view topic_name);
};

} // namespace mqtt
```

### Behaviour

#### Exact match (3.3.1)

Each topic level split from `topic_name` on `/` must match the corresponding
trie node via its exact string key.

#### Single-level wildcard `+` (3.3.2)

A `+` node in the trie matches exactly one topic level.  At each level during
traversal the `+` child is followed in addition to the exact-level child,
subject to the system-topic exclusion below.

#### Multi-level wildcard `#` (3.3.3)

A `#` child is checked at every trie node before descending further.
Subscriptions stored at the `#` node are collected immediately; the `#` node
matches the current level and all subsequent levels (zero or more), so it is
checked at beginning of each recursive step.

#### System topic exclusion (3.3.4)

When `topic_name` begins with `$`, wildcard children (`+` and `#`) at the
**root level only** (depth == 0) are skipped.  After the first exact-level
step the normal wildcard rules apply (e.g. `$SYS/+` and `$SYS/#` do match
`$SYS/info`).

### Constraints

- Does not validate `topic_name` — callers must ensure it contains no wildcards.
- Thread safety: none — mirrors the `SubscriptionTrie` contract.
- Multiple results for the same client are possible when subscriptions overlap;
  callers are responsible for per-client deduplication and QoS merging.
