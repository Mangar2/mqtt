# ini — Generic INI Parser

## Purpose

Provides reusable INI infrastructure for YAHA clients. The parser is domain-agnostic and preserves multi-value keys so each client can map config data to its own runtime structures.

## Public API

### Class `IniDocument`

| Member | Signature | Notes |
|--------|-----------|-------|
| `loadFromFile` | `static IniDocument(const filesystem::path&)` | parses INI text into document model, throws on load/parse failure |
| `findSection` | `const Section*(string_view) const` | returns section pointer or null |
| `lastValue` | `optional<string>(string_view, string_view) const` | returns last value for section/key |
| `parseUnsigned` | `static optional<uint64_t>(string_view, uint64_t, uint64_t)` | bounded unsigned parser |
| `readUnsigned` | `pair<optional<uint64_t>, string>(string_view, string_view, uint64_t, uint64_t) const` | typed unsigned read with value/error result |
| `readBool` | `pair<optional<bool>, string>(string_view, string_view) const` | typed bool read with value/error result |

### Class `IniDocument::Section`

| Member | Signature | Notes |
|--------|-----------|-------|
| `entries` | `const vector<Entry>&() const` | ordered key/value lines as parsed |
| `valuesForKey` | `optional<vector<string>>(string_view) const` | all values for key (multi-value support) |
| `lastValueForKey` | `optional<string>(string_view) const` | last value for key |

### Struct `IniDocument::Entry`

| Field | Type | Notes |
|-------|------|-------|
| `key` | `string` | parsed key |
| `value` | `string` | parsed value |

## Parsing behavior

- INI section headers: `[section]`
- Key/value lines: `key = value`
- Semicolon (`;`) comments are stripped inline; hash (`#`) is treated as a line comment marker only when it is the first character at column zero
- Leading/trailing whitespace is trimmed on keys, values, and section names
- Empty lines are ignored
- Empty section headers are retained as existing sections even when they contain no key/value entries
- Duplicate keys are allowed and preserved as multiple values in insertion order
- Parse errors throw with a descriptive message that includes file context and line number
- Open/read failures include system error id and system error text
- Typed read errors include the complete field path (`section.key`) and raw input value

## Files

| File | Role |
|------|------|
| `ini_document.h` | INI parser API declarations |
| `ini_document.cpp` | parser implementation |
| `test/TEST_SPEC.md` | unit test specification |
| `test/ini_document_test.cpp` | unit tests |
| `test/ini_document_typed_read_test.cpp` | typed read unit tests |
