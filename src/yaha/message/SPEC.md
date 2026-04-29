# message — YAHA Message Type

## Purpose

Defines the universal `Message` value type and its supporting types (`ReasonEntry`, `Qos`,
`Value`). Every YAHA component sends and receives `Message` objects; this is the one data
format shared across the entire YAHA home automation system.

## Public API

### Enums

```cpp
enum class Qos : std::uint8_t {
    AtMostOnce  = 0U,
    AtLeastOnce = 1U,
    ExactlyOnce = 2U
};
```

### Types

```cpp
struct ReasonEntry { std::string message; std::string timestamp; };
using Value = std::variant<std::string, double>;
```

### Class `Message`

| Member | Signature | Notes |
|--------|-----------|-------|
| Constructor | `Message(string topic, Value value, Qos qos = AtLeastOnce, bool retain = false)` | Defaults: qos=1, retain=false |
| `topic()` | `const string& () const` | MQTT topic path |
| `value()` | `const Value& () const` | string or double |
| `qos()` | `Qos () const` | QoS level |
| `retain()` | `bool () const` | retain flag |
| `reason()` | `const vector<ReasonEntry>& () const` | reason chain, most-recent first |
| `isOn()` | `bool () const noexcept` | true for value == 1.0, "on", "ON", "true" |
| `addReason(text)` | `void (string)` | prepends entry with auto-generated ISO 8601 UTC timestamp |
| `addReason(text, ts)` | `void (string, string)` | prepends entry with caller-supplied timestamp |
| `clone()` | `Message () const` | returns a deep copy (value semantics) |
| `validate(msg)` | `static void (const Message&)` | throws `std::invalid_argument` on invalid message |

## Constraints

- Value semantics: `Message` is copyable and movable; pass by `const&` for reading, by value when modifying.
- Reason list: index 0 is the most recent entry; each `addReason` call inserts at the front.
- `validate()` rejects: empty topic, ReasonEntry with empty message field.
- No external dependencies. Header includes only: `<string>`, `<variant>`, `<vector>`, `<cstdint>`.

## Files

| File | Role |
|------|------|
| `message.h` | Type declarations |
| `message.cpp` | Method implementations |
| `test/TEST_SPEC.md` | Unit-test specification |
| `test/message_test.cpp` | Catch2 unit tests |
