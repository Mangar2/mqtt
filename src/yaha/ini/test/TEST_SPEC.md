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
| `load_ini_treats_hash_at_column_zero_as_comment_even_with_equals` | Hash comments at column zero are always comment lines | line starting with `#` and containing `=` | parser ignores the line as comment |
| `load_ini_rejects_indented_hash_line` | Hash comment marker is only valid at column zero | indented line starting with spaces then `#` | parser treats line as invalid key/value and reports missing `=` |
| `load_ini_keeps_empty_section` | Empty section headers must still be discoverable | INI file containing only `[subscriptions]` | `findSection("subscriptions")` returns a section with zero entries |
| `ini_document_parses_bounded_unsigned` | Generic typed parser bounds handling | direct parse calls | valid values pass, invalid values fail |
| `ini_document_reads_optional_unsigned_field` | Optional typed field read | valid `mqtt.port` plus missing key | present key parses to value, missing key returns `nullopt` and fires no warning handler |
| `ini_document_reports_invalid_unsigned_field` | Invalid typed field read fires warning handler | `port = invalid`, add probe handler, `readUnsigned(..., defaultValueText)` | returns `nullopt`; probe receives raw value, given default text, and standardized reason text with `section.key` |
| `ini_document_reads_optional_bool_field` | Optional typed bool field read | valid `mqtt.enabled = yes` plus missing key | present key parses to `true`, missing key returns `nullopt` and fires no warning handler |
| `ini_document_reports_invalid_bool_field` | Invalid typed bool field read fires warning handler | `enabled = maybe`, add probe handler, `readBool(..., defaultValue)` | returns `nullopt`; probe receives raw value, `"true"`/`"false"` text of the given default, and standardized reason text |
| `default_warning_handler_formats_fallback_message_on_stderr` | Default handler is installed automatically and reproduces legacy log format | `loadFromFile(path, "svc")`, then `reportFallback("sec", "key", "raw", "default", "reason")`, capture stderr | stderr contains `svc[warn] config_fallback section=sec key=key value='raw' default='default' reason='reason'` |
| `add_warning_handler_invokes_all_registered_handlers` | Multiple handlers registered via `addWarningHandler` | add two probe handlers plus built-in default, call `reportFallback` once | both probe handlers observe exactly one call with matching fields |
| `clear_warning_handlers_removes_default_handler` | `clearWarningHandlers` removes the built-in default handler too | `clearWarningHandlers()`, add one probe handler, call `reportFallback(...)` | only the probe handler added after clear fires; stderr stays empty |
| `report_fallback_forwards_service_name_and_fields` | `reportFallback` passes `serviceName` from `loadFromFile` and all given fields unchanged | `loadFromFile(path, "myservice")`, add probe handler, call `reportFallback("filestore", "port", "99999", "1", "out of range")` | probe receives `serviceName == "myservice"` and all fields verbatim |
| `load_from_file_defaults_service_name_when_omitted` | Backward-compatible call without `serviceName` still compiles/works | `loadFromFile(path)` (no second argument), add probe handler, call `reportFallback(...)` | probe receives an empty `serviceName` |
