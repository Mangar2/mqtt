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
src/codec/primitive/test/primitive_codec_test.cpp
```

One `*_test.cpp` file per module directory.
The `TEST_SPEC.md` lives in the same `test/` subdirectory.

CMake discovers all `src/*_test.cpp` files automatically — no manual registration needed.

## Running tests — always use the Python script

All commands are run from the **project root**: `c:\Development\mqtt`.
**Never call cmake, ctest, llvm-profdata or llvm-cov directly.** Use only the script.

### Mandatory execution rule

For any code change in broker scope, always run `python3 test/run_coverage_broker.py` before completion.
For any code change in client scope, always run `python3 test/run_coverage_clients.py` before completion.
If the touched scope is unclear or spans both, run both scripts.
Do not stop after editing code without running the matching script(s).

The scripts live at `test/run_coverage_broker.py` and `test/run_coverage_clients.py`.  
All generated files are written to `test/` (`run_broker.log`, `run_clients.log`, `coverage_broker.profdata`, `coverage_clients.profdata`) — never to the project root or `build/`.

### Full workflow (most common)

```sh
python test/run_coverage_broker.py
python test/run_coverage_clients.py
```

Each script runs in sequence: (1) build debug, (2) run scoped tests, (3) build coverage binary, (4) measure scoped coverage.  
Stops immediately on failure and prints a focused error summary.  
On success prints a compact table — tests passed and coverage per production file.  
Full output goes to `test/run_broker.log` or `test/run_clients.log` — read it only when diagnosing a failure.

### Line-level detail for a file below threshold

```sh
python test/run_coverage_broker.py --show src/<module>/<file>.cpp
python test/run_coverage_clients.py --show src/<module>/<file>.cpp
```

### Scoped report (reuses existing profdata — no rebuild)

```sh
python test/run_coverage_broker.py --scope src/<module>/
python test/run_coverage_clients.py --scope src/<module>/
```

> After adding or changing tests, always run the full script first to regenerate profdata.

### Run a specific module's tests only (by Catch2 tag)

```sh
.\build\debug\yahabroker-tests.exe [subscription_trie]
```

## Writing tests

- One `TEST_CASE` per behaviour.
- Use `SECTION` to group related assertions within a case.
- Name test cases as `snake_case` identifiers matching the `TEST_SPEC.md` table.
- Tag every test case with `[<module>]` (e.g. `[primitive]`, `[properties]`).
- Use `CHECK` by default — including for `constexpr` functions. `STATIC_CHECK` is only for cases where compile-time evaluability is itself the property under test (rare). `STATIC_CHECK` generates zero runtime coverage.
- Do not test implementation details — test observable behaviour.
- Never mark a test `[!shouldfail]`, skip it, or comment it out to make the build green.
- Ensure full test coverage vor all platforms (windows, linux, mac-os, ...), always. 
- Dont create giant test files 500 lines ok, more than 700 lines forbidden. Immediately refactor test files that are this large to several test files.

## Exception testing with `[[nodiscard]]` functions

When a test verifies that a `[[nodiscard]]` function **throws**, the return value
is intentionally not used. Suppress the `-Wunused-result` warning with a `(void)` cast:

```cpp
// correct — no compiler warning
try {
    (void)decode_variable_byte_integer(reader);
    FAIL("Expected CodecException");
} catch (const CodecException& e) {
    CHECK(e.error() == CodecError::BufferTooShort);
}
```

Never remove `[[nodiscard]]` from production code to silence this warning.

## Coverage pitfall — `std::visit` with runtime dispatch

Avoid combining `std::visit` (which instantiates the lambda per variant type) with a
**runtime switch** inside the lambda:

```cpp
// BAD — each of the 7 type instantiations compiles all 7 switch cases,
// producing 49 regions of which only 7 are ever reached.
std::visit([expected](const auto& v) noexcept -> bool {
    using T = std::decay_t<decltype(v)>;
    switch (expected) {          // runtime switch inside per-type lambda
        case PropertyDataType::Byte: return std::is_same_v<T, uint8_t>;
        ...
    }
}, value);
```

Use `std::holds_alternative` with a single runtime switch instead — one function,
one region per case, fully coverable:

```cpp
// GOOD — 7 regions, all reachable
switch (expected) {
    case PropertyDataType::Byte:  return std::holds_alternative<uint8_t>(value);
    case PropertyDataType::TwoByteInteger: return std::holds_alternative<TwoByteInteger>(value);
    ...
}
```

## Coverage analysis

Coverage uses **clang source-based instrumentation** (`-fprofile-instr-generate` /
`-fcoverage-mapping`). The `test-coverage` CMake preset enables it automatically.

> **Critical:** Never use `ctest` to collect coverage.
> The Python scripts handle coverage collection via scoped test execution.

**Always use `python test/run_coverage_broker.py` and `python test/run_coverage_clients.py` — never run llvm-profdata or llvm-cov manually.**

### Improving coverage — one file at a time

**Hard rule:** Increase coverage **only** by adding or improving test cases.
Do **not** modify production code solely to improve coverage metrics.
Production code may be changed only when behaviour/spec is incorrect.

When the full report shows files below 80%, work through them **strictly one at a time**:

1. Pick the first file below threshold.
2. Run `--show` for that file only — no other commands in parallel:
   ```sh
    python test/run_coverage_broker.py --show src/<module>/<file>
    python test/run_coverage_clients.py --show src/<module>/<file>
   ```
3. Read the `TEST_SPEC.md` for that module — one file read, nothing else in parallel.
4. Analyse: which uncovered lines correspond to which missing test cases?
5. Write or extend the test, then run both scoped scripts to verify:
    ```sh
    python test/run_coverage_broker.py
    python test/run_coverage_clients.py
    ```
6. Only after that file reaches ≥ 80 %: move to the next file below threshold.

Mandatory rule: if you did not improve coverage by adding a test to reach 80% refactor the source code to improve its unit-testability instead of just add new test cases.

**Never run `--show` for multiple files at once.** The output volume makes it
impossible to reason about each file correctly.

### Reading the report columns

| Column | What it measures | Threshold |
|--------|-----------------|-----------|
| **Regions Cover** | % of source code regions executed (finest grain) | ≥ 80 % |
| **Functions Executed** | % of functions called at least once | ≥ 80 % |
| **Lines Cover** | % of source lines executed | ≥ 80 % |
| **Branches Cover** | % of branch paths taken | informational only |

**Target:** production files must reach ≥ 80 % on Regions, Functions, and Lines.
Branch coverage in `*_test.cpp` files will typically read ~50 % — normal and not a failure.

---

### Coverage in the completion report

```
### Coverage — <module name>

<paste the scoped llvm-cov report table here>

Production headers: Regions <X>%, Functions <X>%, Lines <X>%
→ threshold met ✓  /  NOT met ✗ (list files below threshold)
```

---

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
