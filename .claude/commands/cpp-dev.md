C++20 coding rules for this project.

## Standard

- Use **C++20** exclusively. No C++17 fallbacks, no compiler extensions .
- Use C++20 features: concepts, ranges, std::span, std::format,
  coroutines, consteval, constinit, designated initializeers.
- Use designated initialisers for aggregates.
- Use range algorithms

## Compiler warnings

The build runs with `-Wall -Wextra -Wpedantic -Werror`.  
**Every warning is a build error. All warnings must be resolved before a task is complete.**

Acceptable resolutions:
- Fix the code (preferred).
- Suppress with a targeted `[[maybe_unused]]`, `(void)x`, or a narrowly-scoped
  `#pragma clang diagnostic` with a comment explaining why.

Never suppress warnings globally or disable `-Werror`.

## OS-specific code

- **One OS per file.** Never implement code for two different operating systems
  in the same `.cpp` file. A file with `#ifdef _WIN32 … #else … #endif` for
  diverging implementations is forbidden.
- **Make the OS visible in the filename.** Use the suffix `_win32.cpp` for
  Windows-only code and `_posix.cpp` for POSIX-only code.
  Example: `tcp_connection_win32.cpp` / `tcp_connection_posix.cpp`.
- **Never include `windows.h` in any header — not even indirectly.**
  `winsock2.h` includes `windows.h`; only include it in `_win32.cpp` files.
  All `.h` files must be free of platform SDK headers.
- **CMakeLists.txt must filter sources by platform.** After globbing, add:
  ```cmake
  if(WIN32)
      list(FILTER MQTT_ALL_SOURCES EXCLUDE REGEX "_posix\\.cpp$")
  else()
      list(FILTER MQTT_ALL_SOURCES EXCLUDE REGEX "_win32\\.cpp$")
  endif()
  ```
  Apply the same filter to test sources.

## Checklist — run before writing any code

1. **Variable/parameter names ≥ 3 chars.** `ex`, `fd`, `op`, `fn`, `id` are all forbidden. Use `hdl`, `opt`, `buf`, `idx`, etc.
2. **`const` on methods** that do not modify member variables.
3. **Braces on every `if`/`else`/`for`/`while` body** — even single-statement ones.
4. **Enum base type** — always specify: `enum class Foo : std::uint8_t { … }`.
5. **`_posix.cpp` files** — wrap entire content in `#if !defined(_WIN32) … #endif` so IntelliSense on Windows stays silent.
6. **Remove unused `#include`s** — only include headers whose symbols are directly used.
7. **Integer suffixes uppercase** — `7U`, `0xFFU`, `1ULL`, never `7u`, `0xffu`.
8. **Do not start a build while `get_errors` reports errors.** Fix all errors first.

## Code style

- No raw owning pointers — use `std::unique_ptr` / `std::shared_ptr`.
- No `new` / `delete` outside of custom allocators.
- Prefer `[[nodiscard]]` on functions whose return value must not be ignored.
- Mark implementation-only symbols in anonymous namespaces.
- Constants: `constexpr` or `constinit`, prefixed `k_` (e.g. `k_max_clients`).
- One class / struct per header file.
- Include order: own header, C++ standard library, third-party, project headers.
- **Variable and parameter names ≥ 3 characters.** Forbidden: `ex`, `fd`, `op`, `fn`, `id`, single letters. Use `hdl`, `opt`, `buf`, `idx`, `val`, `pkt`, `len`, `err`.

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

Length of all variable and parameter names ≥ 3. Forbidden 2-char names include: `ex`, `fd`, `op`, `fn`, `id`, `to`, `ok`.

### Trailing member documentation

Use `///<` (not `//`) for inline member comments:

```cpp
uint32_t value{0};  ///< Encoded integer value; must not exceed k_max_value.
```

### Style rules

- Use `/** */` block style for all standalone doc blocks (not `///`).
- Do **not** repeat what the type signature already says — focus on *why* and *constraints*.
- Enum values use `///<` trailing comments when the name alone is ambiguous.