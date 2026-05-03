# error_handling — YAHA Unified Error Model

## Purpose

Provides a single YAHA error class for all component error outputs and throw paths.
The model carries machine-readable code, technical message, user-facing message,
and optional debug details.

## Public API

### Class `YahaError` : `std::runtime_error`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `YahaError(string code, string message, string userMessage, optional<string> debugDetails = nullopt)` | Builds throw-capable error object with full context |
| `errorCode` | `const string&() const noexcept` | Stable machine-readable code |
| `message` | `const string&() const noexcept` | Technical message |
| `userMessage` | `const string&() const noexcept` | User-facing message |
| `debugDetails` | `const optional<string>&() const noexcept` | Optional diagnostics |
| `buildMessage` | `string() const` | Composes one output message string |

## Behavior

- `YahaError` can be thrown as standard C++ exception (`std::runtime_error`).
- `buildMessage()` returns a deterministic message string with all available members.
- If `debugDetails` is not set, the details segment is omitted.

## Files

| File | Role |
|------|------|
| `yaha_error.h` | Public declarations |
| `yaha_error.cpp` | Implementation |
