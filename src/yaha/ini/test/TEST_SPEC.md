# ini test specification

## Scope

Unit tests for generic INI parser behavior and multi-value key support.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `load_ini_with_sections_and_values` | Parse valid INI with multiple sections | file with `[mqtt]` and `[server]` keys | parser succeeds, lastValue returns expected values |
| `load_ini_preserves_multivalue_keys` | Duplicate keys in same section | section with repeated `module` key | valuesForKey returns all values in insertion order |
| `load_ini_rejects_missing_equals` | Invalid key/value line | line without `=` | parser fails with line-specific error |
| `load_ini_rejects_empty_section_name` | Invalid section line | `[]` | parser fails with line-specific error |
| `load_ini_supports_hash_and_inline_comments` | Accept hash line comments and semicolon inline comments without breaking MQTT wildcard/hash values | file with `#` line comment, semicolon inline comment, and `#` in values | parser succeeds, line comments are removed and `#` values remain intact |
| `ini_document_parses_bounded_unsigned` | Generic typed parser bounds handling | direct parse calls | valid values pass, invalid values fail |
| `ini_document_reads_optional_unsigned_field` | Optional typed field read | valid `mqtt.port` plus missing key | present key parses, missing key is no-op |
| `ini_document_reports_invalid_unsigned_field` | Invalid typed field read | `port = invalid` | read fails and returns standardized error text with `section.key` |
