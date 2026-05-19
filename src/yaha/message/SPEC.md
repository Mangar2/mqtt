# message — YAHA Message Type

## Purpose

Defines the universal `Message` value type and its supporting types (`ReasonEntry`, `Qos`,
`Value`) plus shared message-envelope payload parser/builder utilities. Every YAHA component
sends and receives `Message` objects; this is the one data format shared across the entire YAHA
home automation system.

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

### Payload Codec Helpers

```cpp
std::string escapeJsonString(std::string_view);
std::string serializeReasonArrayOldestFirst(const std::vector<ReasonEntry>&);
std::string buildEnvelopePayload(const Message&);
std::optional<Value> parseValueToken(std::string_view);
std::optional<std::vector<ReasonEntry>> parseReasonArray(std::string_view);
std::optional<Message> parseEnvelopePayload(const std::string&, const std::string&, Qos, bool, bool);
bool validateEnvelopeShape(std::string_view);
```

Notes:
- `buildEnvelopePayload` emits canonical YAHA transport envelope JSON.
- `serializeReasonArrayOldestFirst` writes reason entries in oldest-first wire order.
- `parseEnvelopePayload` validates topic consistency (`message.topic` must match MQTT topic).
- `parseValueToken` supports canonical string/number values and accepts `true`/`false`/`null`
    as string tokens for backward compatibility behavior already used by transports.
- String parsing decodes JSON escapes for control tokens and ASCII unicode escapes (`\\u00XX`).
- Regression tests in `test/message_payload_codec_test.cpp` pin TS-reference-compatible
    envelope semantics (message shape, escaping, and reason ordering).

### Class `Message`

| Member | Signature | Notes |
|--------|-----------|-------|
| Constructor | `Message(string topic, Value value, Qos qos = AtLeastOnce, bool retain = false, bool dup = false)` | Defaults: qos=1, retain=false, dup=false |
| `topic()` | `const string& () const` | MQTT topic path |
| `value()` | `const Value& () const` | string or double |
| `qos()` | `Qos () const` | QoS level |
| `retain()` | `bool () const` | retain flag |
| `dup()` | `bool () const` | MQTT DUP flag for publish semantics |
| `reason()` | `const vector<ReasonEntry>& () const` | reason chain, most-recent first |
| `rawPayload()` | `const optional<string>& () const` | optional original transport payload for lossless forwarding |
| `isOn()` | `bool () const noexcept` | true for value == 1.0, "on", "ON", "true" |
| `addReason(text)` | `void (string)` | prepends entry with auto-generated ISO 8601 UTC timestamp |
| `addReason(text, ts)` | `void (string, string)` | prepends entry with caller-supplied timestamp |
| `setDup(dup)` | `void (bool)` | updates MQTT DUP flag |
| `setRawPayload(payload)` | `void (string)` | stores original transport payload bytes as string |
| `clearRawPayload()` | `void () noexcept` | removes optional raw payload |
| `clone()` | `Message () const` | returns a deep copy (value semantics) |
| `validate(msg)` | `static void (const Message&)` | throws `std::invalid_argument` on invalid message |

## Constraints

- Value semantics: `Message` is copyable and movable; pass by `const&` for reading, by value when modifying.
- Reason list: index 0 is the most recent entry; each `addReason` call inserts at the front.
- DUP flag is part of message state and can be propagated by transports for QoS>0 duplicate-delivery semantics.
- `rawPayload()` is optional and carries exact original payload text when an adapter chooses lossless forwarding.
- `validate()` rejects: empty topic, ReasonEntry with empty message field.
- Envelope payload parser/builder functions are the shared format utility for YAHA modules and
    must be reused instead of private per-module envelope implementations.
- No external dependencies. Header includes only: `<string>`, `<variant>`, `<vector>`, `<cstdint>`.

## Files

| File | Role |
|------|------|
| `message.h` | Type declarations |
| `message.cpp` | Method implementations |
| `message_payload_codec.h` | Shared YAHA envelope parser/builder declarations |
| `message_payload_codec.cpp` | Shared YAHA envelope parser/builder implementations |
| `test/TEST_SPEC.md` | Unit-test specification |
| `test/message_test.cpp` | Catch2 unit tests |
| `test/message_payload_codec_test.cpp` | Catch2 unit tests for parser/builder edge cases |
