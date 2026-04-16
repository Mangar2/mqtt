Full workflow for creating or extending code in this project. Follow every step in order.

## 0. CMakeLists.txt registration — mandatory for every new file

**Never read CMakeLists.txt or CMakePresets.json.** The relevant facts are recorded here once:

- Production sources → `add_executable(mqtt-broker …)` block: add every new `src/**/*.cpp` file.
- Test sources → `add_executable(mqtt-broker-tests …)` block: add every new `src/**/test/*_test.cpp` file.
- Test framework: **Catch2 v3** (`Catch2::Catch2WithMain`), already linked to the test target.
- Compile flags: `MQTT_COMPILE_OPTIONS` is already applied to both targets — do not add per-file flags.
- Build presets and commands: see `/build` skill.

After creating or deleting any `.cpp` file, update `CMakeLists.txt` in the same step.
Verify the build compiles without errors before considering the task done.

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
