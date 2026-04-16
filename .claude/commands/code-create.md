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

## 7. Build and verify — mandatory final step

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

**Success criteria:**
- Build output ends with no errors (warnings are errors due to `-Werror`).
- `ctest` reports `100% tests passed`.
- The new module's tests appear by name in the ctest output.

Do not report a module as complete until both conditions are met.
