Full workflow for creating or extending code in this project. Follow every step in order.

## 0. CMakeLists.txt — no manual file registration needed

**Never edit CMakeLists.txt for source file registration.** It uses `file(GLOB_RECURSE … CONFIGURE_DEPENDS)` to discover sources automatically:

- `src/*.cpp` (excluding any path containing `/test/`) → compiled into `mqtt-broker`.
- `src/*_test.cpp` (any depth) → compiled into `mqtt-broker-tests`.

CMake re-runs the glob at every build invocation and reconfigures automatically when files are added or removed. No manual edits are needed.

Other facts to know (do not read CMakePresets.json):
- Test framework: **Catch2 v3** (`Catch2::Catch2WithMain`), already linked to the test target.
- Compile flags: `MQTT_COMPILE_OPTIONS` (`-Wall -Wextra -Wpedantic -Werror`) is applied to both targets.
- Both targets have `src/` on their include path, so headers are included as `data_model/…`.
- Build presets and commands: see `/build` skill.

## 1. File count rule

Keep the number of code files per directory (and subdirectory) below 10.
Before adding a new file, check the current count. If a directory is near the limit, split into subdirectories first.

## 2. Documentation-first: target spec (SPEC.md)

Before writing any code, create or update a `SPEC.md` in the target directory.

- One `SPEC.md` per directory.
- A `SPEC.md` in a parent/main directory summarises the contents of all its subdirectories.
- `SPEC.md` describes: purpose, public API / interfaces, data structures, behaviour, constraints.
- Use `SPEC.md` as the implementation guide — the code must match it.

## 3. Documentation and code stay in sync

- When new insights emerge during coding that contradict `SPEC.md`: update `SPEC.md` first, then continue coding.
- Before implementing a new feature: update `SPEC.md` first, then implement based on the updated spec.
- Never let code and documentation diverge.

## 4. No redundant regeneration

Only generate or rewrite code when behaviour actually changes.
Never regenerate a file with identical behaviour. If a file is correct, leave it alone.

## 5. Unit-test specification (TEST_SPEC.md)

For every directory with testable code, maintain a `TEST_SPEC.md` alongside `SPEC.md`.

- `TEST_SPEC.md` lists every planned unit test: name, scenario, input, expected output/behaviour.
- Before writing new tests: add them to `TEST_SPEC.md` first, then implement.
- Keep `TEST_SPEC.md` current — remove deleted tests, add entries for new behaviour.

## 6. Unit-test implementation

Generate unit tests based on `TEST_SPEC.md`.

- Place test files in a `test/` subdirectory inside the directory of the code under test.
- The `TEST_SPEC.md` lives inside that `test/` directory.
- Follow the `/unit-test` skill for test style and execution.
- Never implement a test not listed in `TEST_SPEC.md` — update the spec file first.

## 7. Build and verify

Every implementation ends with a successful build **and** all tests passing.
Run the following commands from the **project root** (`c:\Development\mqtt`):

```sh
cmake --build --preset debug && ctest --preset debug
```

If this is the first build or after a clean:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## 8. Completion checklist — mandatory gate

Do not mark any module or feature as complete until **every item** below passes.
This list is intentionally extensible — add new entries whenever a new class of issue is
discovered.

| # | Criterion | How to verify |
|---|-----------|---------------|
| 1 | **Build clean** | `cmake --build --preset debug` exits with zero errors |
| 2 | **No compiler warnings** | Guaranteed by `-Werror` — any warning is a build failure |
| 3 | **No linter / IDE warnings** | All clang-tidy diagnostics in the IDE panel are resolved; target state is zero warnings |
| 4 | **All tests pass** | `ctest --preset debug` reports `100% tests passed`; the new module's tests appear by name |
| 5 | **Test coverage ≥ 90 %** | Line and branch coverage for the new or changed module is at least 90 % |
| 6 | **SPEC.md is current** | Every touched directory has a SPEC.md that accurately describes the final implementation |
| 7 | **TEST_SPEC.md is current** | Every test in code has a matching entry; removed tests are removed from the spec |
| 8 | **Doxygen on all public API** | Every header in touched directories follows the documentation rules in `/cpp-dev` |
