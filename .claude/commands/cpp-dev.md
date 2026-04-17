C++20 coding rules for this project.

## Standard

- Use **C++20** exclusively. No C++17 fallbacks, no compiler extensions .
- Use C++20 features: concepts, ranges, std::span, std::format,
  coroutines, consteval, constinit, designated initializeers.
- Use designated initialisers for aggregates.

## Compiler warnings

The build runs with `-Wall -Wextra -Wpedantic -Werror`.  
**Every warning is a build error. All warnings must be resolved before a task is complete.**

Acceptable resolutions:
- Fix the code (preferred).
- Suppress with a targeted `[[maybe_unused]]`, `(void)x`, or a narrowly-scoped
  `#pragma clang diagnostic` with a comment explaining why.

Never suppress warnings globally or disable `-Werror`.

## Code style

- No raw owning pointers — use `std::unique_ptr` / `std::shared_ptr`.
- No `new` / `delete` outside of custom allocators.
- Prefer `[[nodiscard]]` on functions whose return value must not be ignored.
- Mark implementation-only symbols in anonymous namespaces.
- Constants: `constexpr` or `constinit`, prefixed `k_` (e.g. `k_max_clients`).
- One class / struct per header file.
- Include order: own header, C++ standard library, third-party, project headers.
- **Variable names must be at least 3 characters long.** Single-letter and two-letter
  variable names are forbidden — even in loop indices, lambdas, and test files.
  Use descriptive names (`idx`, `buf`, `pkt`, `val`, etc.).

## Project-wide conventions

These rules apply to every file in the codebase and must **not** be repeated in
module-level `SPEC.md` files.

- `#pragma once` on every header file (no `#ifndef` include guards).
- All production types live in the `mqtt` namespace.
- All exception types derive from `std::runtime_error`.
- `operator==` provided via `= default` on every plain-data (`struct`) type.

## Documentation

All header files must use **Doxygen** format exclusively. Plain `//` comments are not
permitted for API-level documentation.

### File header

Every `.h` file starts with a `@file` block placed **after** `#pragma once`, before the
first `#include`:

```cpp
/**
 * @file <filename>.h
 * @brief <One-line description ending with a period>.
 */
```

### Types and free functions

Every `struct`, `class`, `enum class`, `using` alias, and free function gets a `/** @brief … */`
doc block directly above its declaration:

```cpp
/**
 * @brief Short description.
 *
 * Optional longer explanation, constraints, or references.
 */
struct Foo { … };
```

### Parameters and return values

Document non-obvious parameters and return values with `@param` and `@return`:

```cpp
/**
 * @brief Returns the encoded size of this value on the wire.
 * @return Byte count in the range [1, 4].
 */
[[nodiscard]] constexpr uint8_t encoded_size() const noexcept;
```

Length of all variable and parameter names >=3

### Trailing member documentation

Use `///<` (not `//`) for inline member comments:

```cpp
uint32_t value{0};  ///< Encoded integer value; must not exceed k_max_value.
```

### Style rules

- Use `/** */` block style for all standalone doc blocks (not `///`).
- Do **not** repeat what the type signature already says — focus on *why* and *constraints*.
- Enum values use `///<` trailing comments when the name alone is ambiguous.