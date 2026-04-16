# Unit Test Guidelines

## Framework

Catch2 v3 (`Catch2::Catch2WithMain`), already linked to the test target.

## Test file location

Tests live **co-located** with the production code they test:

```
src/<module>/test/<module>_test.cpp
```

Examples:
```
src/data_model/types/test/types_test.cpp
src/data_model/message/test/message_test.cpp
```

One `*_test.cpp` file per module directory.
The `TEST_SPEC.md` lives in the same `test/` subdirectory.

CMake discovers all `src/*_test.cpp` files automatically — no manual registration needed.

## Running tests — exact commands

All commands are run from the **project root**: `c:\Development\mqtt`

### First time or after a clean build

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

### Incremental build + test (most common)

CMake reconfigures automatically when files are added or removed:

```sh
cmake --build --preset debug && ctest --preset debug
```

### With memory and undefined-behaviour checks (recommended before merging)

```sh
cmake --preset debug-sanitize
cmake --build --preset debug-sanitize
ctest --preset debug-sanitize
```

### Run only tests for a specific module (by tag)

```sh
./build/debug/mqtt-broker-tests.exe [message]
./build/debug/mqtt-broker-tests.exe [packet]
```

### Run a single test by name

```sh
./build/debug/mqtt-broker-tests.exe "message_defaults"
```

### List all registered tests

```sh
./build/debug/mqtt-broker-tests.exe --list-tests
```

## Test output binary

```
build/debug/mqtt-broker-tests.exe
build/debug-sanitize/mqtt-broker-tests.exe
```

## Writing tests

- One `TEST_CASE` per behaviour.
- Use `SECTION` to group related assertions within a case.
- Name test cases as `snake_case` identifiers matching the `TEST_SPEC.md` table.
- Tag every test case with `[<module>]` (e.g. `[message]`, `[packet]`).
- Use `STATIC_CHECK` for `constexpr` expressions, `CHECK` for runtime assertions.
- Do not test implementation details — test observable behaviour.
- Never mark a test `[!shouldfail]`, skip it, or comment it out to make the build green.

## Analysing test failures

When a test fails:

1. Read the full failure output — assertion, file, line, expected vs. actual.
2. Locate the relevant `SPEC.md` and `TEST_SPEC.md` in the module directory.
3. Decide: is the **implementation wrong** or is the **test wrong**?
   - Spec clearly defines the behaviour and implementation deviates → fix the code.
   - Test asserts something that contradicts the spec → fix the test.
   - Genuinely ambiguous → ask before proceeding.
4. State the conclusion in one sentence before making any change.
5. Fix **either** the code **or** the test — never both in the same step unless provably independent.
