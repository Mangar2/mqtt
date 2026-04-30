# ini — Generic INI Parser

## Purpose

Provides reusable INI infrastructure for YAHA clients. The parser is domain-agnostic and preserves multi-value keys so each client can map config data to its own runtime structures.

## Public API

### Class `IniDocument`

| Member | Signature | Notes |
|--------|-----------|-------|
| `tryLoadFromFile` | `static bool(const filesystem::path&, IniDocument&, string&)` | parses INI text into document model |
| `findSection` | `const Section*(string_view) const` | returns section pointer or null |
| `lastValue` | `optional<string>(string_view, string_view) const` | returns last value for section/key |

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

### Generic helper functions

| Function | Signature | Notes |
|----------|-----------|-------|
| `iniLookupLastValue` | `optional<string>(const IniDocument&, string_view, string_view)` | generic section/key lookup helper |
| `iniTryParseUnsigned` | `bool(const string&, uint64_t, uint64_t, uint64_t&)` | bounded unsigned parser |
| `iniTryReadUnsigned` | `bool(const IniDocument&, string_view, string_view, uint64_t, uint64_t, uint64_t&, string_view, string&)` | optional typed reader with standardized error text |

## Parsing behavior

- INI section headers: `[section]`
- Key/value lines: `key = value`
- Semicolon comments (`;`) are stripped per line
- Leading/trailing whitespace is trimmed on keys, values, and section names
- Empty lines are ignored
- Duplicate keys are allowed and preserved as multiple values in insertion order
- Parse errors return `false` and a descriptive `errorMessage` with line number

## Files

| File | Role |
|------|------|
| `ini_document.h` | INI parser API declarations |
| `ini_document.cpp` | parser implementation |
| `ini_value_reader.h` | generic value reader API declarations |
| `ini_value_reader.cpp` | generic value reader implementation |
| `test/TEST_SPEC.md` | unit test specification |
| `test/ini_document_test.cpp` | unit tests |
| `test/ini_value_reader_test.cpp` | value reader unit tests |
