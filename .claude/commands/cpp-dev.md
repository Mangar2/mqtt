C++20 coding rules for this project.

## Standard

- Use **C++20** exclusively. No C++17 fallbacks, no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`).
- Prefer C++20 features where they improve clarity: concepts, ranges, `std::span`, `std::format`,
  coroutines, designated initialisers, `consteval`, `constinit`.

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
